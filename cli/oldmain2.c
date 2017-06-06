/*
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 *
 * This is published as vos-install-cli, but it's basically an automated
 * Arch Linux-with-custom-packages installer. This does not deal with
 * partitioning, as that is way too easy to get wrong (and I would) and
 * cause a lot of damage. Maybe later.
 *
 * This program must be run as root. I recommend pkexec for GUI apps.
 *
 * The installer takes a number of arguments, either over command line
 * or from STDIN. Using STDIN can help avoid showing sensitive information
 * such as the password on the command line arguments. The arguments are:
 *
 * -d  --dest      The volume to install Arch at, in /dev form.
 * -h  --hostname  The hostname to use for the installed system.
 *                   NONE to not set.
 * -u  --username  The username of the default user account, or
 *                   NONE to not create a default user.
 * -p  --password  The password of the default user account, and
 *                   the password for the root account. Set NONE
 *                   for automatic login.
 * -l  --locale    The system locale, or NONE to not set.
 * -z  --zone      The timeline file, relative to /usr/share/zoneinfo.
 *                   NONE to not set.
 * -k  --packages  A list of extra packages to install along with
 *                   base. Package names should be separated by spaces.
 *                   NONE for no extra packages.
 * -s  --services  A list of systemd services to enable in the installed
 *                   arch, separated by spaces.
 * -v  --verbose   Set for extra output. This must be on command line args.
 *
 * All arguments (except --verbose) can be passed over STDIN in the
 * form ^<argname>=<value>$ where ^ means start of line and $ means
 * end of line (regex!). So, to set the password, you could pipe
 * \npassword=bad_password\n. An argument can only be set once; any
 * subsequent attempts at setting an argument will be ignored.
 *
 * If the installer needs a flag that has not been set yet, either by
 * command line or by STDIN, it will output "WAITING <argname>\n" and
 * pause until the argument is passed through STDIN.
 *
 * Steps this installer takes:
 *
 * 1) Mounts volume <dest> at <mount>
 * 2) $ pacstrap <mount> base <packages>
 * 3) Changes root into <mount>
 * 4) $ genfstab / >> /etc/fstab
 * 5) $ passwd <password>
 * 6) Updates locale.gen with <locale> and $ locale-gen
 * 7) $ ln -s /usr/share/zoneinfo/<zone> /etc/localtime
 * 8) echo <hostname> > /etc/hostname
 * 9) Create user account with username and password and group wheel
 * 10) Enables wheel to access sudo
 * 11) Enables <services>
 *
 * STDOUT/ERR from child processess are piped to this program's STDOUT/ERR,
 * and this program also outputs "PROGRESS <%f>\n" where %f is from 0 to 100
 * as progress is made. A successful install has exit code 0, and any errors
 * from child processess cause the install to fail with the child process's
 * exit code.
 *
 * Sending SIGINT to this process will cleanly exit it, but will not undo
 * changes made.
 */

#include <gio/gio.h>
#include <stdlib.h>
#include <argp.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <string.h>

static struct argp_option options[] =
{
	{"dest",      'd', "block device", 0, "The volume to install Arch at"},
	{"hostname",  'h', "name",      0, "Machine hostname"},
	{"username",  'u', "name",      0, "Default account username"},
	{"password",  'p', "password",  0, "Root/default account password"},
	{"locale",    'l', "locale",    0, "Locale (locale.gen format)"},
	{"zone",      'z', "file",      0, "Timezone file (relative to /usr/share/zoneinfo/)"},
	{"packages",  'k', "packages",  0, "List of extra packages separated by spaces"},
	{"services",  's', "services",  0, "List of systemd services to enable"},
	{"verbose",   'v',              0, 0, "Be verbose"},
	{0}
};

const char *argp_program_version = "vos-install-cli 0.1";
const char *argp_program_bug_address = "Aidan Shafran <zelbrium@gmail.com>";
static char argp_program_doc[] = "An installer for VeltOS (Arch Linux). See top of main.c for detailed instructions on how to use the installer. The program author is not responsible for any damages, including but not limited to exploded computer, caused by this program. Use as root and with caution.";

const guint kMaxSteps = 3;

typedef struct
{
	// Args
	gchar *dest;
	gchar *hostname;
	gchar *username;
	gchar *password;
	gchar *locale;
	gchar *zone;
	gchar *packages;
	gchar *services;
	gboolean verbose;
	
	// Running data
	GMainLoop *loop;
	gboolean done;
	GCancellable *cancellable;
	guint exitCode;
	guint steps;
	gchar *mountPath;
} Data;

typedef void (*RunCommandComplete)(Data *d, GInputStream *stdout);

static error_t parse_arg(int key, char *arg, struct argp_state *state);
static void progress(Data *d);
static void run_command(Data *d, RunCommandComplete cb, const gchar *command);
static void mount_volume(Data *d);
static void run_pacstrap(Data *d);
static void run_genfstab(Data *d);



int main(int argc, char **argv)
{
	Data *d = g_new0(Data, 1);
	static struct argp argp = {options, parse_arg, NULL, argp_program_doc};
	error_t error;
	if((error = argp_parse(&argp, argc, argv, 0, 0, d)))
		return error;
	
	d->loop = g_main_loop_new(NULL, FALSE);
	
	mount_volume(d);
	
	if(!d->done)
		g_main_loop_run(d->loop);
	g_main_loop_unref(d->loop);
	
	guint code = d->exitCode;
	g_free(d->dest);
	g_free(d->hostname);
	g_free(d->username);
	g_free(d->password);
	g_free(d->locale);
	g_free(d->zone);
	g_free(d->packages);
	g_free(d->services);
	g_clear_object(&d->cancellable);
	g_free(d->mountPath);
	g_free(d);
	return code;
}

static error_t parse_arg(int key, char *arg, struct argp_state *state)
{
	Data *d = state->input;
	arg = g_strdup(arg);
	switch (key)
	{
	case 'd': d->dest = arg; break;
	case 'h': d->hostname = arg; break;
	case 'u': d->username = arg; break;
	case 'p': d->password = arg; break;
	case 'l': d->locale = arg; break;
	case 'z': d->zone = arg; break;
	case 'k': d->packages = arg; break;
	case 's': d->services = arg; break;
	case 'v': d->verbose = TRUE; break;
	default: g_free(arg); return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

static inline void progress(Data *d)
{
	printf("Progress: %f\n", (gfloat)(d)->steps / kMaxSteps);
}

#define printfnl(fmt, ...) printf(fmt "\n", __VA_ARGS__)

#define EXIT(d, code, x, fmt...) { \
	progress(d); \
	printfnl(fmt, 0); \
	x; \
	(d)->done = TRUE; \
	(d)->exitCode = code; \
	g_main_loop_quit((d)->loop); \
	return; \
}

typedef struct
{
	GCallback callback;
	gpointer data;
} Closure;

#define RUN(d, cb, collect, args...) { \
	gchar *cmd=g_strjoin(" ", args, NULL); printf("Running: %s\n", cmd); g_free(cmd); \
	GError *error = NULL; \
	GSubprocess *proc = g_subprocess_new(collect ? (G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE) : G_SUBPROCESS_FLAGS_NONE, &error, args, NULL); \
	g_object_set_data(G_OBJECT(proc), "collect", (void *)collect); \
	if(error) \
		EXIT(d, 1, g_error_free(error), "%s", error->message) \
	run_command_wait(d, cb, proc); \
}

static void run_command_finish(GSubprocess *proc, GAsyncResult *res, Closure *clos);
static void run_command_wait(Data *d, RunCommandComplete cb, GSubprocess *proc)
{
	Closure *closure = g_new(Closure, 1);
	closure->callback = G_CALLBACK(cb); closure->data = d;
	g_subprocess_wait_async(proc,
		d->cancellable,
		(GAsyncReadyCallback)run_command_finish,
		closure);
}
	
static void run_command_finish(GSubprocess *proc, GAsyncResult *res, Closure *clos)
{
	RunCommandComplete cb = (RunCommandComplete)clos->callback;
	Data *d = clos->data;
	g_free(clos);
	
	GError *error = NULL;
	g_subprocess_wait_finish(proc, res, &error);
	if(error)
		EXIT(d, 1, g_error_free(error);g_object_unref(proc), "%s", error->message)
	
	gint status = g_subprocess_get_exit_status(proc);
	if(status != 0)
		EXIT(d, status, g_object_unref(proc), "Command failed with exit code %i", status)
	
	GInputStream *stdout = NULL;
	if(g_object_get_data(G_OBJECT(proc), "collect"))
		stdout = g_subprocess_get_stdout_pipe(proc);

	d->steps++;
	if(cb)
		cb(d, stdout);
	g_object_unref(proc);
}

// Checks if the arg is available (non-NULL)
// If it isn't, reads and parses STDIN until it is non-NULL
static void ensure_argument(Data *d, gchar **arg)
{
	GIOChannel *ch = NULL; 
	while(*arg == NULL)
	{
		gchar *line = NULL;
		gsize len = 0;
		if(!ch) ch = g_io_channel_unix_new(STDIN_FILENO);
		if(g_io_channel_read_line(ch, &line, &len, NULL, NULL) != G_IO_STATUS_NORMAL)
			exit(1);
		if(!line)
			continue;
		
		// +2 because g_io_channel_read_line includes the \n at the end
		#define TRY(x, n) if(len >= ((n)+2) && !d->x && strncmp(line, #x "=", (n)+1) == 0) { d->x = g_strndup(line+((n)+1), len-((n)+2)); }
		TRY(password, 8)
		else TRY(dest, 4)
		else TRY(hostname, 8)
		else TRY(username, 8)
		else TRY(locale, 6)
		else TRY(zone, 4)
		else TRY(packages, 8)
		else TRY(services, 8)
		#undef TRY
	}
	
	if(ch) g_io_channel_unref(ch);
}


static void mount_volume_finish(Data *d, GInputStream *stdout);
static void mount_volume(Data *d)
{
	ensure_argument(d, &d->dest);

	RUN(d, mount_volume_finish, TRUE, "udisksctl", "mount", "-b", d->dest);
}

static void mount_volume_finish(Data *d, GInputStream *stdout)
{
	char buf[300];
	gsize num = g_input_stream_read(stdout, buf, 300, NULL, NULL);
	printf("buf: %.*s\n", num, buf);
}

//static void (GVolume *volume, GAsyncResult *res, Data *d);
//static void mount_volume(Data *d)
//{
//	GVolume *dest = d->p.destination;
//	
//	if(!dest || !G_IS_VOLUME(dest))
//		ABORT_AND_RETURN(d, "Invalid destination volume");
//	
//	OUTPUT(d, "Mounting %s", g_volume_get_name(dest));
//	
//	// Skip mounting if the volume is already mounted
//	GMount *mount = NULL;
//	if((mount = g_volume_get_mount(dest)))
//	{
//		d->mount = mount; // g_volume_get_mount is [return full]
//		mount_volume_finish(NULL, NULL, d);
//		return;
//	}
//	
//	if(!g_volume_can_mount(dest))
//		ABORT_AND_RETURN(d, "Unable to mount volume");
//	
//	g_volume_mount(dest,
//		G_MOUNT_MOUNT_NONE,
//		g_mount_operation_new(),
//		d->p.cancellable,
//		(GAsyncReadyCallback)mount_volume_finish,
//		d);
//}
//
//static void mount_volume_finish(GVolume *volume, GAsyncResult *res, Data *d)
//{
//	gboolean newlyMounted = !d->mount;
//	if(newlyMounted)
//	{
//		GError *error = NULL;
//		if(!volume
//		|| !res
//		|| !g_volume_mount_finish(volume, res, &error)
//		|| error
//		|| !(d->mount = g_volume_get_mount(volume)))
//		{
//			if(error)
//			{
//				abortinstall(d, error->message);
//				g_error_free(error);
//				return;
//			}
//			ABORT_AND_RETURN(d, "Error mounting volume");
//		}
//	}
//	
//	GFile *root = g_mount_get_root(d->mount);
//	d->mountPath = g_file_get_path(root);
//	g_object_unref(root);
//	
//	if(newlyMounted)
//	{
//		OUTPUT(d, "Drive mounted at %s", d->mountPath);
//	}
//	else
//	{
//		OUTPUT(d, "Drive already mounted at %s", d->mountPath);
//	}
//	
//	d->steps++;
//	run_pacstrap(d);
//}
//
//
//static void run_pacstrap_finish(Data *d);
//static void run_pacstrap(Data *d)
//{
//	run_command_f(d, run_pacstrap_finish,
//		"pkexec pacstrap %s base %s", d->mountPath, d->p.packages ? d->p.packages : "");
//}
//
//static void run_pacstrap_finish(Data *d)
//{
//	run_genfstab(d);
//}
//
//static void run_genfstab_finish(Data *d);
//static void run_genfstab(Data *d)
//{
//	run_command_f(d, run_genfstab_finish,
//		"pkexec genfstab %s >> %s/etc/fstab", d->mountPath, d->mountPath);
//}
//
//static void run_genfstab_finish(Data *d)
//{
//	g_message("yay");
//}
