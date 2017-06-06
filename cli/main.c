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
 *                   "NONE" via command line or "hostname=\n" on STDIN to
 *                   not set.
 * -u  --username  The username of the default user account, or
 *                   NONE/blank STDIN to not create a default user.
 * -p  --password  The password of the default user account, and
 *                   the password for the root account. Set NONE/blank STDIN
 *                   for automatic login.
 * -l  --locale    The system locale, or NONE/blank STDIN to not set.
 * -z  --zone      The timeline file, relative to /usr/share/zoneinfo.
 *                   NONE/blank STDIN to not set.
 * -k  --packages  A list of extra packages to install along with
 *                   base. Package names should be separated by spaces.
 *                   NONE/blank STDIN for no extra packages.
 * -s  --services  A list of systemd services to enable in the installed
 *                   arch, separated by spaces. Or NONE/blank STDIN for no
 *                   extra services.
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

const guint kMaxSteps = 4;

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
	GCancellable *cancellable;
	guint steps;
	gchar *mountPath;
} Data;

static error_t parse_arg(int key, char *arg, struct argp_state *state);
static void progress(Data *d);
static gint mount_volume(Data *d);
static gint run_pacstrap(Data *d);
static gint run_genfstab(Data *d);
static gint run_chroot(Data *d);


int main(int argc, char **argv)
{
	Data *d = g_new0(Data, 1);
	static struct argp argp = {options, parse_arg, NULL, argp_program_doc};
	error_t error;
	if((error = argp_parse(&argp, argc, argv, 0, 0, d)))
		return error;

	#define REPL_NONE(val) if(g_strcmp0(val, "NONE") == 0) { g_free(val); val = g_strdup(""); }
	REPL_NONE(d->hostname);
	REPL_NONE(d->username);
	REPL_NONE(d->password);
	REPL_NONE(d->locale);
	REPL_NONE(d->zone);
	REPL_NONE(d->packages);
	REPL_NONE(d->services);
	#undef REPL_NONE
	
	gint code = mount_volume(d);
	
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

#define println(fmt, ...) printf(fmt "\n", __VA_ARGS__)

static inline void progress(Data *d)
{
	println("Progress: %f", (gfloat)(d)->steps / kMaxSteps);
}

static inline void step(Data *d)
{
	d->steps++;
	progress(d);
}


#define EXIT(d, code, x, fmt...) { \
	progress(d); \
	println(fmt, 0); \
	x; \
	return code; \
}

// Use like: gint status = RUN(..)
#define RUN(d, process, stdout, args...) 0; { \
	gchar *argv [] = {args, NULL}; \
	status = run(d, process, stdout, (const gchar * const *)argv); \
}

// Returns the process's exit code as a negative, to distinugish a child
// process error (possibly not fatal) from a GSubprocess error (fatal).
// If stdout is non-NULL, then process must also be non-NULL, and the caller
// must call g_object_unref on process after using the stdout stream.
static gint run(Data *d, GSubprocess **process, GInputStream **stdout, const gchar * const *args)
{
	if(process) *process = NULL;
	if(stdout) *stdout = NULL;
	
	gchar *cmd = g_strjoinv(" ", (gchar **)args);
	println("Running: %s", cmd);
	g_free(cmd);
	
	GError *error = NULL;
	GSubprocess *proc = g_subprocess_newv(args, stdout ? (G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE) : G_SUBPROCESS_FLAGS_NONE, &error);
	
	if(error)
		EXIT(d, 1, g_error_free(error), "%s", error->message)
	
	if(!g_subprocess_wait(proc, d->cancellable, &error) || error)
	{
		if(error)
			EXIT(d, 1, g_error_free(error), "%s", error->message)
		EXIT(d, 1, , "Process cancelled")
	}
	
	int exit = g_subprocess_get_exit_status(proc);
	
	if(stdout)
		*stdout = g_subprocess_get_stdout_pipe(proc);
	if(process)
		*process = proc;
	else
		g_object_unref(proc);
	return (-exit);
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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

// Works similarly to chroot, with two exceptions:
// 1) This automatically changes working directory to
//    the chroot. That is, cwd will always return /
//    after a call successful to exitable_chroot.
// 2) Passing NULL for path returns to the original root
//    at the time of first calling exitable_chroot (or
//    previous NULL call).
// The effect of #2 does not stack:
//  exitable_chroot("/a");
//  exitable_chroot("/b");
//  exitable_chroot(NULL);
// Would leave you at the root of where /a exists, not
// the root where /b exists.
// Returns TRUE on success. On failure, the working directory
// may be changed, but the chroot will not be.
static gboolean exitable_chroot(const gchar *path)
{
	static int originalRoot = -1;
	if(originalRoot < 0)
		originalRoot = open("/", O_RDONLY);
	if(originalRoot < 0)
		return FALSE;
	
	int tmpcwd = open(".", O_RDONLY);
	if(tmpcwd < 0)
		return FALSE;
	
	if((path && chdir(path)) || (!path && fchdir(originalRoot)))
	{
		close(tmpcwd);
		return FALSE;
	}
	
	if(chroot("."))
	{
		// Try to move back to previous directory. This could fail,
		// in which case the cwd might change.
		fchdir(tmpcwd);
		close(tmpcwd);
		return FALSE;
	}

	close(tmpcwd);
	if(!path)
	{
		close(originalRoot);
		originalRoot = -1;
	}
	return TRUE;
}


static gint mount_volume(Data *d)
{
	ensure_argument(d, &d->dest);
	
	// Let udisks do the mounting, since it mounts the drive in a
	// unique spot, unlike simply mounting at /mnt.
	// Could do this with udisks dbus API but lazy.
	
	GInputStream *stdout = NULL;
	GSubprocess *proc = NULL;
	gint status = RUN(d, &proc, &stdout, "udisksctl", "mount", "-b", d->dest);
	if(status > 0)
		return status;
	status = -status;
	gchar buf[1024];
	gsize num = g_input_stream_read(stdout, buf, 1024, NULL, NULL);
	g_object_unref(proc);
	
	if(status == 0)
	{
		gchar *phrase = g_strdup_printf("Mounted %s at", d->dest);
		int phraseLen = strlen(phrase);
		const gchar *loc = g_strstr_len(buf, num, phrase);
		g_free(phrase);
		if(!loc)
			EXIT(d, status, , "Unexpected output: %.*s", num, buf);
		loc += phraseLen + 1; // +1 for space
		const gchar *end = strchr(loc, '.');
		if(!end)
			EXIT(d, status, , "Unexpected output: %.*s", num, buf);
		d->mountPath = g_strndup(loc, (end-loc));
	}
	else
	{
		static char phrase[] = "already mounted at";
		const gchar *loc = g_strstr_len(buf, num, phrase);
		if(!loc)
			EXIT(d, status, , "Mount failed: %.*s", num, buf);
		loc += sizeof(phrase) + 1; // +1 for " `", and the other +1 is the NULL byte
		const gchar *end = strchr(loc, '\'');
		if(!end)
			EXIT(d, status, , "Unexpected output: %.*s", num, buf);
		d->mountPath = g_strndup(loc, (end-loc));
		println("%s already mounted", d->dest);
	}
	
	println("Mounted at %s", d->mountPath);
	step(d);
	return run_pacstrap(d);
}

static gint run_pacstrap(Data *d)
{
	ensure_argument(d, &d->packages);
	gchar ** split = g_strsplit(d->packages, " ", -1);
	guint numPackages = 0;
	for(guint i=0;split[i]!=NULL;++i)
		if(split[i][0] != '\0') // two spaces between packages create empty splits
			numPackages++;
	
	gchar **args = g_new(gchar *, numPackages + 4);
	args[0] = "pacstrap";
	args[1] = d->mountPath;
	args[2] = "base";
	for(guint i=0,j=3;split[i]!=NULL;++i)
		if(split[i][0] != '\0')
			args[j++] = split[i];
	args[numPackages+3] = '\0';
		
	gint status = run(d, NULL, NULL, (const gchar * const *)args);
	g_free(args);
	g_strfreev(split);
	
	if(status > 0)
		return status;
	else if(status < 0)
		EXIT(d, status, , "Pacstrap failed with code %i.", -status)
	
	step(d);
	return run_genfstab(d);
}

static gint run_genfstab(Data *d)
{
	GInputStream *stdout = NULL;
	GSubprocess *proc = NULL;
	gint status = RUN(d, &proc, &stdout, "genfstab", d->mountPath);
	if(status > 0)
		return status;
	else if(status < 0)
		EXIT(d, status, g_object_unref(proc), "Genfstab failed with code %i.", -status)
	
	gchar *path = g_build_path("/", d->mountPath, "etc/fstab");
	println("Writing generated fstab to %s", path);
	gint fstab = open(path, O_WRONLY);
	if(fstab < 0)
		EXIT(d, 1, g_free(path);g_object_unref(proc), "Failed to open fstab for writing")
	g_free(path);
	
	char buf[1024];
	gsize num = 0;
	while((num = g_input_stream_read(stdout, buf, 1024, NULL, NULL)) > 0)
	{
		if(write(fstab, buf, num) != num)
			EXIT(d, 1, close(fstab);g_object_unref(proc), "Failed to write %i bytes to fstab", num)
	}	
	close(fstab);
	
	step(d);
	return run_chroot(d);
}

static gint run_chroot(Data *d)
{
	println("Changing root to %s", d->mountPath);
	if(!exitable_chroot(d->mountPath))
		return 1;
	step(d);
}
