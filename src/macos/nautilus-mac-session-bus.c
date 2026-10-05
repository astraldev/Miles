/*
 * Copyright (C) 2026 Ekure Edem
 *
 * Nautilus is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * Nautilus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; see the file COPYING.  If not,
 * see <http://www.gnu.org/licenses/>.
 *
 */

/* MacOS has no D-Bus session bus. Nautilus starts its own, for gvfs. */

#define G_LOG_DOMAIN "nautilus-mac"

#include <config.h>
#include "nautilus-mac-session-bus.h"
#include "nautilus-mac-paths.h"

#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

/* After dbus's session.conf. The address given to dbus-daemon replaces the one here. */
#define BUS_CONFIG \
        "<busconfig>" \
        "<type>session</type>" \
        "<keep_umask/>" \
        "<listen>unix:tmpdir=/tmp</listen>" \
        "<auth>EXTERNAL</auth>" \
        "<servicedir>%s</servicedir>" \
        "<policy context=\"default\">" \
        "<allow send_destination=\"*\" eavesdrop=\"true\"/>" \
        "<allow eavesdrop=\"true\"/>" \
        "<allow own=\"*\"/>" \
        "</policy>" \
        "</busconfig>"
#define BUS_START_TIMEOUT_MS 3000
#define BUS_STOP_TIMEOUT_MS 1000

/* 0 if the bus was already running. */
static GPid bus_pid = 0;
/* The bus that was already running: one a crashed Nautilus left, unless another one runs. */
static GPid found_bus_pid = 0;
static gboolean owns_found_bus = FALSE;

static char *original_gio_modules = NULL;
static gboolean set_runtime_dir = FALSE;
static gboolean set_bus_address = FALSE;

static void
add_gio_modules (void)
{
    const char *modules = g_getenv ("GIO_EXTRA_MODULES");
    g_autofree char *gvfs_modules = nautilus_mac_get_install_path (NAUTILUS_GIO_MODULE_DIR);
    g_autofree char *with_gvfs = NULL;

    original_gio_modules = g_strdup (modules);

    if (modules == NULL || *modules == '\0')
    {
        g_setenv ("GIO_EXTRA_MODULES", gvfs_modules, TRUE);

        return;
    }

    with_gvfs = g_strconcat (gvfs_modules, G_SEARCHPATH_SEPARATOR_S, modules, NULL);
    g_setenv ("GIO_EXTRA_MODULES", with_gvfs, TRUE);
}

/* Not in /tmp, which MacOS sweeps. */
static char *
create_runtime_dir (void)
{
    g_autofree char *path = g_build_filename (g_get_user_cache_dir (), "nautilus", NULL);
    GStatBuf info;

    if (g_mkdir_with_parents (path, 0700) != 0)
    {
        return NULL;
    }

    if (g_lstat (path, &info) != 0 ||
        !S_ISDIR (info.st_mode) ||
        info.st_uid != getuid () ||
        (info.st_mode & 077) != 0)
    {
        return NULL;
    }

    return g_steal_pointer (&path);
}

static gboolean
bus_is_running (const char *socket_path,
                GPid       *pid)
{
    struct sockaddr_un address = { .sun_family = AF_UNIX };
    socklen_t pid_size = sizeof (*pid);
    gboolean is_running;
    int fd;

    if (g_strlcpy (address.sun_path, socket_path, sizeof (address.sun_path)) >=
        sizeof (address.sun_path))
    {
        return FALSE;
    }

    fd = socket (AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        return FALSE;
    }

    is_running = (connect (fd, (struct sockaddr *) &address, sizeof (address)) == 0);
    if (is_running && pid != NULL &&
        getsockopt (fd, SOL_LOCAL, LOCAL_PEERPID, pid, &pid_size) != 0)
    {
        *pid = 0;
    }
    close (fd);

    return is_running;
}

static void
stop_bus (GPid     pid,
          gboolean is_child)
{
    kill (pid, SIGTERM);

    for (int waited_ms = 0;
         is_child ? waitpid (pid, NULL, WNOHANG) == 0 : kill (pid, 0) == 0;
         waited_ms += 10)
    {
        /* A bus that ignores the signal must not hang Nautilus. */
        if (waited_ms >= BUS_STOP_TIMEOUT_MS)
        {
            kill (pid, SIGKILL);
            if (is_child)
            {
                waitpid (pid, NULL, 0);
            }
            break;
        }

        g_usleep (10 * G_TIME_SPAN_MILLISECOND);
    }
}

/* These files hold full paths, and the app may have been moved. */
static char *
copy_install_files (const char *built_in_dir,
                    const char *runtime_dir,
                    const char *name)
{
    g_autofree char *source_dir = nautilus_mac_get_install_path (built_in_dir);
    char *target_dir = g_build_filename (runtime_dir, name, NULL);
    g_autoptr (GDir) sources = g_dir_open (source_dir, 0, NULL);
    g_autoptr (GDir) targets = NULL;
    /* Quoted for the command line, escaped for the key file. */
    g_autofree char *quoted_prefix = g_shell_quote (nautilus_mac_get_prefix ());
    g_autoptr (GString) prefix = g_string_new (quoted_prefix);
    const char *file_name;

    g_string_replace (prefix, "\\", "\\\\", 0);
    g_mkdir (target_dir, 0700);

    /* What an earlier version of the app left. */
    targets = g_dir_open (target_dir, 0, NULL);
    while (targets != NULL && (file_name = g_dir_read_name (targets)) != NULL)
    {
        g_autofree char *target = g_build_filename (target_dir, file_name, NULL);

        g_unlink (target);
    }

    while (sources != NULL && (file_name = g_dir_read_name (sources)) != NULL)
    {
        g_autofree char *source = g_build_filename (source_dir, file_name, NULL);
        g_autofree char *target = g_build_filename (target_dir, file_name, NULL);
        g_autofree char *contents = NULL;

        if (g_file_get_contents (source, &contents, NULL, NULL))
        {
            g_autoptr (GString) moved = g_string_new (contents);

            g_string_replace (moved, NAUTILUS_PREFIX, prefix->str, 0);
            g_file_set_contents (target, moved->str, moved->len, NULL);
        }
    }

    return target_dir;
}

static gboolean
spawn_bus (const char *bus_address,
           const char *socket_path,
           const char *runtime_dir)
{
    g_autofree char *daemon = nautilus_mac_get_install_path (NAUTILUS_DBUS_DAEMON);
    g_autofree char *services_dir = copy_install_files (NAUTILUS_BUS_SERVICES_DIR,
                                                        runtime_dir, "bus-services");
    g_autofree char *mounts_dir = copy_install_files (NAUTILUS_GVFS_MOUNTS_DIR,
                                                      runtime_dir, "gvfs-mounts");
    g_autofree char *config = g_markup_printf_escaped (BUS_CONFIG, services_dir);
    g_autofree char *config_path = g_build_filename (runtime_dir, "bus.conf", NULL);
    g_autofree char *config_argument = g_strconcat ("--config-file=", config_path, NULL);
    g_autofree char *address_argument = g_strconcat ("--address=", bus_address, NULL);
    g_autofree char *certificates = nautilus_mac_get_install_path (NAUTILUS_DATADIR "/certificates.pem");
    /* gvfs is started by the bus, and gets its environment. */
    g_auto (GStrv) envp = g_environ_setenv (g_get_environ (), "GVFS_MOUNTABLE_DIR", mounts_dir, TRUE);
    const char *argv[] =
    {
        daemon,
        config_argument,
        address_argument,
        "--nofork",
        "--nopidfile",
        NULL
    };
    g_autoptr (GError) error = NULL;

    g_file_set_contents (config_path, config, -1, NULL);

    /* Only an app that carries its files has them: see bundle-app.sh. */
    if (g_file_test (certificates, G_FILE_TEST_EXISTS))
    {
        envp = g_environ_setenv (envp, "GVFS_TLS_CERTIFICATES", certificates, TRUE);
    }

    if (!g_spawn_async (NULL, (char **) argv, envp,
                        G_SPAWN_DO_NOT_REAP_CHILD |
                        G_SPAWN_STDOUT_TO_DEV_NULL |
                        G_SPAWN_STDERR_TO_DEV_NULL,
                        NULL, NULL, &bus_pid, &error))
    {
        g_warning ("Could not start the session bus: %s", error->message);
        bus_pid = 0;

        return FALSE;
    }

    for (int waited_ms = 0; waited_ms < BUS_START_TIMEOUT_MS; waited_ms += 10)
    {
        if (bus_is_running (socket_path, NULL))
        {
            return TRUE;
        }

        if (waitpid (bus_pid, NULL, WNOHANG) == bus_pid)
        {
            g_warning ("The session bus exited as it started");
            bus_pid = 0;

            return FALSE;
        }

        g_usleep (10 * G_TIME_SPAN_MILLISECOND);
    }

    g_warning ("The session bus did not start");
    stop_bus (bus_pid, TRUE);
    bus_pid = 0;

    return FALSE;
}

/* Call in main() before anything uses GIO, which reads these variables once. */
void
nautilus_mac_session_bus_start (void)
{
    g_autofree char *created_runtime_dir = NULL;
    const char *runtime_dir;
    g_autofree char *socket_path = NULL;
    g_autofree char *bus_address = NULL;
    g_autofree char *lock_path = NULL;
    gboolean is_running;
    int lock_fd;

    add_gio_modules ();

    if (g_getenv ("DBUS_SESSION_BUS_ADDRESS") != NULL)
    {
        return;
    }

    runtime_dir = g_getenv ("XDG_RUNTIME_DIR");
    if (runtime_dir == NULL)
    {
        created_runtime_dir = create_runtime_dir ();
        if (created_runtime_dir == NULL)
        {
            g_warning ("Could not create a folder for the session bus");

            return;
        }

        runtime_dir = created_runtime_dir;
        g_setenv ("XDG_RUNTIME_DIR", runtime_dir, TRUE);
        set_runtime_dir = TRUE;
    }

    socket_path = g_build_filename (runtime_dir, "bus", NULL);
    bus_address = g_strconcat ("unix:path=", socket_path, NULL);
    lock_path = g_build_filename (runtime_dir, "bus.lock", NULL);

    /* Two Nautilus starting together must not both start a bus. */
    lock_fd = g_open (lock_path, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (lock_fd >= 0)
    {
        flock (lock_fd, LOCK_EX);
    }

    is_running = bus_is_running (socket_path, &found_bus_pid);
    if (!is_running)
    {
        g_unlink (socket_path);
        is_running = spawn_bus (bus_address, socket_path, runtime_dir);
    }

    if (lock_fd >= 0)
    {
        close (lock_fd);
    }

    if (is_running)
    {
        g_setenv ("DBUS_SESSION_BUS_ADDRESS", bus_address, TRUE);
        set_bus_address = TRUE;
    }
}

char **
nautilus_mac_session_bus_get_launch_environ (void)
{
    char **envp = g_get_environ ();

    if (original_gio_modules != NULL)
    {
        envp = g_environ_setenv (envp, "GIO_EXTRA_MODULES", original_gio_modules, TRUE);
    }
    else
    {
        envp = g_environ_unsetenv (envp, "GIO_EXTRA_MODULES");
    }

    if (set_runtime_dir)
    {
        envp = g_environ_unsetenv (envp, "XDG_RUNTIME_DIR");
    }

    if (set_bus_address)
    {
        envp = g_environ_unsetenv (envp, "DBUS_SESSION_BUS_ADDRESS");
    }

    return envp;
}

void
nautilus_mac_session_bus_take_over (void)
{
    owns_found_bus = TRUE;
}

void
nautilus_mac_session_bus_stop (void)
{
    if (bus_pid != 0)
    {
        stop_bus (bus_pid, TRUE);
    }
    else if (owns_found_bus && found_bus_pid != 0)
    {
        stop_bus (found_bus_pid, FALSE);
    }
    else
    {
        return;
    }

    bus_pid = 0;
    found_bus_pid = 0;
}
