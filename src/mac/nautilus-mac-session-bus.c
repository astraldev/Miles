/*
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

/* macOS has no D-Bus session bus. Nautilus starts its own, for gvfs. */

#define G_LOG_DOMAIN "nautilus-mac"

#include <config.h>
#include "nautilus-mac-session-bus.h"

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

#define BUS_CONFIG_FILE NAUTILUS_DATADIR "/dbus-session.conf"
#define BUS_START_TIMEOUT_MS 3000

/* 0 if the bus was already running. */
static GPid bus_pid = 0;

static char *original_gio_modules = NULL;
static gboolean set_runtime_dir = FALSE;
static gboolean set_bus_address = FALSE;

static void
add_gio_modules (void)
{
    const char *modules = g_getenv ("GIO_EXTRA_MODULES");
    g_autofree char *with_gvfs = NULL;

    original_gio_modules = g_strdup (modules);

    if (modules == NULL || *modules == '\0')
    {
        g_setenv ("GIO_EXTRA_MODULES", NAUTILUS_GIO_MODULE_DIR, TRUE);

        return;
    }

    with_gvfs = g_strconcat (NAUTILUS_GIO_MODULE_DIR, G_SEARCHPATH_SEPARATOR_S, modules, NULL);
    g_setenv ("GIO_EXTRA_MODULES", with_gvfs, TRUE);
}

/* Not in /tmp, which macOS sweeps. */
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
bus_is_running (const char *socket_path)
{
    struct sockaddr_un address = { .sun_family = AF_UNIX };
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
    close (fd);

    return is_running;
}

static gboolean
spawn_bus (const char *bus_address,
           const char *socket_path)
{
    g_autofree char *address_argument = g_strconcat ("--address=", bus_address, NULL);
    const char *argv[] =
    {
        NAUTILUS_DBUS_DAEMON,
        "--config-file=" BUS_CONFIG_FILE,
        address_argument,
        "--nofork",
        "--nopidfile",
        NULL
    };
    g_autoptr (GError) error = NULL;

    if (!g_spawn_async (NULL, (char **) argv, NULL,
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
        if (bus_is_running (socket_path))
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
    nautilus_mac_session_bus_stop ();

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

    is_running = bus_is_running (socket_path);
    if (!is_running)
    {
        g_unlink (socket_path);
        is_running = spawn_bus (bus_address, socket_path);
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
nautilus_mac_session_bus_stop (void)
{
    if (bus_pid == 0)
    {
        return;
    }

    kill (bus_pid, SIGTERM);
    waitpid (bus_pid, NULL, 0);
    bus_pid = 0;
}
