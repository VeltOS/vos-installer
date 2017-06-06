#include <gio/gio.h>

#define ARCH_INSTALL_ERROR_QUARK arch_install_error_quark()
GQuark arch_install_error_quark(void);

/*
 * Callback as progress is made installing arch.
 *
 * userdata: Userdata from ArchInstallParameters
 *
 * line:     A line of output, which may be prinited to the screen.
 *           NULL if there is no output for this callback.
 *
 * progress: Percent progress from [0,100].
 *
 * error:    If non-NULL, the install has failed and this will be the
 *           last callback.
 */
typedef void (*ArchInstallerOutputCallback)(gpointer userdata, const gchar *line, gfloat progress, GError *error);

/*
 * destination: The GVolume to install arch on.
 *
 * hostname: The hostname (computer name) of the arch installation.
 *           NULL to not set a hostname.
 *
 * username: Username of the initial user account.
 *           NULL to not create a user account.
 *
 * password: Password of the initial user account, and also root password.
 *           NULL to use no password.
 *
 * locale:   The system locale
 *           NULL to not set locale.
 *
 * zone:     The timezone file (relative to /usr/share/zoneinfo/)
 *           NULL to not set timezone.
 *
 * packages: Packages to install, separated by spaces (base is always insalled)
 *           Empty string or NULL to install no extra packages.
 *
 * verbose:  More detail sent to output callback.
 *
 * callback: Function to call as progress changes or output is produced.
 *           NULL for none.
 *
 * userdata: Data to pass to callback.
 *
 * cancellable: A GCancellable to abort the install operation, or NULL.
 *              install_arch will steal the referene to this, so use
 *              g_object_ref if you want to keep it for yourself.
 */

typedef struct
{
	GVolume *destination;
	gchar *hostname;
	gchar *username;
	gchar *password;
	gchar *locale;
	gchar *zone;
	gchar *packages;
	gboolean verbose;
	ArchInstallerOutputCallback callback;
	gpointer userdata;
	GCancellable *cancellable;
} ArchInstallParameters;

/*
 * Installs Arch using the given parameters (which may be stack or
 * heap-allocated).
 * Returns immediately, use the output callback for progress info.
 */
void install_arch(ArchInstallParameters *parameters);
