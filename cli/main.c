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
 * -n  --name      The real name of the default user account, or
 *                   NONe/blank STDIN to not set.
 * -p  --password  The password of the default user account, and
 *                   the password for the root account. Set NONE/blank STDIN
 *                   for automatic login.
 * -l  --locale    The system locale, or NONE/blank STDIN to default
 *                   to "en_US.UTF-8". This functions as a prefix; setting
 *                   the argument to "en" would enable all english locales.
 *                   The first match is used to set the LANG variable.
 * -z  --zone      The timeline file, relative to /usr/share/zoneinfo.
 *                   NONE/blank STDIN to use /usr/share/zoneinfo/UTC.
 * -k  --packages  A list of extra packages to install along with
 *                   base. Package names should be separated by spaces.
 *                   NONE/blank STDIN for no extra packages.
 *                   If sudo is specified among the list of packages, the
 *                   wheel group will automatically be enabled for sudo.
 * -s  --services  A list of systemd services to enable in the installed
 *                   arch, separated by spaces. Or NONE/blank STDIN for no
 *                   extra services.
 *     --skippacstrap  Skips the package installation.
 *     --ext4      If present, runs mkfs.ext4 on the destination volume
 *                   before installing. This will erase all contents on
 *                   the volume. Without this, this installer can be used
 *                   to upgrade or fix an existing arch installation.
 *                   Without this argument, the installation will fail
 *                   if the volume is not a Linux-capable filesystem.
 *     --kill      Specify the path to a fifo. If any data is written
 *                   to this fifo, the installer will immediately abort.
 *     --postcmd   A shell command to run within the chroot of the new
 *                   Arch install at the very end of the installation.
 *                   This argument may be specified multiple times to
 *                   run multiple scripts.
 *     --repo     This installer generates a new /etc/pacman.conf on the
 *                   target machine. This flag specifies a custom repo to
 *                   add (before installing packages), in the format
 *                   "Name,Server,SigLevel,keys...". Where 'keys' are the
 *                   PGP signing key(s) (full fingerprint only), if any,
 *                   that should be downloaded from a keyserver and added
 *                   to pacman's keyring.
 *
 * All arguments an be passed over STDIN in the
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
 * 3) $ genfstab <mount> >> <mount>/etc/fstab
 * 4) Changes root into <mount>
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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libudev.h>

static struct argp_option options[] =
{
	{"dest",      'd', "block device", 0, "The volume to install Arch at"},
	{"hostname",  'h', "name",      0, "Machine hostname"},
	{"username",  'u', "name",      0, "Default account username"},
	{"name",      'n', "name",      0, "Real name of default user"},
	{"password",  'p', "password",  0, "Root/default account password"},
	{"locale",    'l', "locale",    0, "Locale (locale.gen format)"},
	{"zone",      'z', "file",      0, "Timezone file (relative to /usr/share/zoneinfo/)"},
	{"packages",  'k', "packages",  0, "List of extra packages separated by spaces"},
	{"services",  's', "services",  0, "List of systemd services to enable"},
	{"ext4",      998, "new filesystem label",           OPTION_ARG_OPTIONAL, "Erases the destination volume and writes a new ext4 filesystem. Optionally specify a parameter which will become the new filesystem label."},
	{"skippacstrap", 999, 0,        0, "Skips pacstrap, to avoid reinstalling all packages if they're already installed"},
	{"kill",      997, "kill",           0, "Optionally specify the path to a fifo. If any data is received at this fifo, the install will be aborted immediately."},
	{"postcmd",   996, "postcmd",     0, "Optionally specify a shell command to run after installation. This may be specified multiple times."},
	{"repo",      995, "repo",      0, "Specify a pacman repository to add to /etc/pacman.conf on the target machine, in the format \"Name,Server,SigLevel,Keys...\" where keys are full PGP fingerprints to download public keys to add to pacman's keyring."},
	{0}
};

const char *argp_program_version = "vos-install-cli 0.1";
const char *argp_program_bug_address = "Aidan Shafran <zelbrium@gmail.com>";
static char argp_program_doc[] = "An installer for VeltOS (Arch Linux). See top of main.c for detailed instructions on how to use the installer. The program author is not responsible for any damages, including but not limited to exploded computer, caused by this program. Use as root and with caution.";

const guint kMaxSteps = 14;

typedef struct
{
	char *name;
	char *server;
	char *siglevel;
	char **keys;
} Repo;

typedef struct
{
	// Args
	gchar *dest;
	gchar *hostname;
	gchar *username;
	gchar *name;
	gchar *password;
	gchar *locale;
	gchar *zone;
	gchar *packages;
	gchar *services;
	gboolean skipPacstrap;
	gboolean writeExt4;
	gchar *newFSLabel; // Only if writeExt4
	GList *postcmds;
	GList *repos;
	
	// Running data
	GCancellable *cancellable;
	guint steps;
	gchar *mountPath;
	gboolean enableSudoWheel;
	char *killfifo;
	char *partuuid;
	char *ofstype; // original fs type before running mkfs.ext4, or NULL if none
} Data;

static error_t parse_arg(int key, char *arg, struct argp_state *state);
static void progress(Data *d);
static gint start(Data *d);
static gint run_ext4(Data *d);
static gint mount_volume(Data *d);
static gint run_pacstrap(Data *d);
static gint run_genfstab(Data *d);
static gint run_chroot(Data *d);
static gint set_passwd(Data *d);
static gint set_locale(Data *d);
static gint set_zone(Data *d);
static gint set_hostname(Data *d);
static gint create_user(Data *d);
static gint enable_services(Data *d);
static gint run_postcmd(Data *d);

static int pgid = 0;
static gboolean killing = FALSE;


// Can be called from different threads. I don't think race conditions
// are a problem here (between checking 'killing' and setting it to TRUE)
// since pgid is set at program start, and it's just killing everything
// everything anyway.
static void stopinstall(gboolean wait)
{
	if(killing)
		return;
	
	// Make sure no new processes start
	killing = TRUE;
	// rejoin parent's process group, so that 'kill' below doesn't kill us
	setpgid(0, getpgid(getppid()));
	// stop all descendants (or those with same pgid)
	kill(-pgid, SIGINT);
	if(wait)
	{
		// Wait 1 second; if the process did get killed, the program
		// should exit before this finishes
		sleep(1);
		// try again
		kill(-pgid, SIGKILL);
	}
}

// Thread
// Wait for data to be sent to the killfifo
static void * killfifo_watch(void *path)
{
	int fifo = open((const char *)path, O_RDONLY);
	char x[2];
	x[0] = '\0';
	x[1] = '\0';
	read(fifo, &x, 1);
	stopinstall(TRUE);
	return NULL;
}

// Thread
// Constantly make sure the parent doesn't change (will
// happen if the original parent dies). If it does, abort.
static void * parent_watch(void *data)
{
	int ppid = getppid();
	do
	{
		sleep(1);
	} while(getppid() == ppid);
	printf("\nParent changed, stopping install\n\n");
	stopinstall(TRUE);
}

static void sigterm_handler(int signum)
{
	//printf("\nReceived %i, stopping install\n\n", signum);
	static gboolean called = FALSE;
	if(called)
		exit(1);
	else
	{
		// Don't wait; a user who presses ctrl+c will see a delay
		stopinstall(FALSE);
		called = TRUE;
	}
}

static void free_repo_struct(Repo *r)
{
	g_free(r->name);
	g_free(r->server);
	g_free(r->siglevel);
	g_strfreev(r->keys);
	g_free(r);
}


int main(int argc, char **argv)
{
	// Don't buffer stdout, seems to mess with GSubprocess/GInputStream
	setbuf(stdout, NULL);

	// New process group, for killfifo
	setpgid(0, 0);
	pgid = getpgid(0);

	// Watch for int/term/hup
	signal(SIGHUP, sigterm_handler);
	signal(SIGINT, sigterm_handler);
	signal(SIGTERM, sigterm_handler);
	// TODO: Use sigaction, since apparently signal() has
	// undefined behavior when multiple threads are involved.
	//struct sigaction act;
	//act.sa_sigaction = sigio;
	//sigemptyset(&act.sa_mask);
	//act.sa_flags = SA_SIGINFO;
	//sigaction(SIGIO, &act, NULL);

	Data *d = g_new0(Data, 1);
	static struct argp argp = {options, parse_arg, NULL, argp_program_doc};
	error_t error;
	if((error = argp_parse(&argp, argc, argv, 0, 0, d)))
		return error;
	
	pthread_t p, p2;
	
	if(d->killfifo)
		pthread_create(&p, NULL, killfifo_watch, d->killfifo);
	pthread_create(&p2, NULL, parent_watch, NULL);

	#define REPL_NONE(val) if(g_strcmp0(val, "NONE") == 0) { g_free(val); val = g_strdup(""); }
	REPL_NONE(d->hostname);
	REPL_NONE(d->username);
	REPL_NONE(d->name);
	REPL_NONE(d->password);
	REPL_NONE(d->locale);
	REPL_NONE(d->zone);
	REPL_NONE(d->packages);
	REPL_NONE(d->services);
	#undef REPL_NONE
	
	gint code = start(d);
	
	g_free(d->dest);
	g_free(d->hostname);
	g_free(d->username);
	g_free(d->name);
	g_free(d->password);
	g_free(d->locale);
	g_free(d->zone);
	g_free(d->packages);
	g_free(d->services);
	g_clear_object(&d->cancellable);
	g_free(d->mountPath);
	g_list_free_full(d->postcmds, g_free);
	g_list_free_full(d->repos, (GDestroyNotify)free_repo_struct);
	g_free(d);
	return code;
}

static Repo * parse_repo_string(const gchar *arg)
{
	// 0,    1,      2,        3...
	// name, server, siglevel, keys...
	
	char **split = g_strsplit(arg, ",", -1);
	guint length = g_strv_length(split);
	guint numkeys = length - 3;
	
	if(length < 3)
	{
		g_strfreev(split);
		return NULL;
	}
	
	// Make sure all keys specified are valid fingerprints
	// Also remove spaces and 0x if present
	for(guint i=3; i<length; ++i)
	{
		char *key = g_strstrip(split[i]);
		guint len = strlen(key);
		
		// Remove 0x if it's there
		if(len >= 2 && key[0] == '0' && (key[1] == 'x' || key[1] == 'X'))
		{
			// len+1 to move back NULL terminator too
			for(guint k=2; k<len+1; ++k)
				key[k-2] = key[k];
			len -= 2;
		}
		
		for(guint j=0; j<len; ++j)
		{
			if(key[j] == ' ')
			{
				for(guint k=j+1; k<len+1; ++k)
					key[k-1] = key[k];
				--len;
				--j;
			}
			else if(!g_ascii_isxdigit(key[j]))
			{
				g_strfreev(split);
				return NULL;
			}
		}
		
		if(len != 40) // The size of a PGP fingerprint
		{
			g_strfreev(split);
			return NULL;
		}
	}
	
	Repo *r = g_new(Repo, 1);
	r->name = g_strstrip(split[0]);
	r->server = g_strstrip(split[1]);
	r->siglevel = g_strstrip(split[2]);
	r->keys = g_new(char *, numkeys+1);
	for(guint i=0; i<numkeys; ++i)
		r->keys[i] = split[3+i];
	r->keys[numkeys] = NULL;
	
	g_free(split);
	return r;
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
	case 'n': d->name = arg; break;
	case 'p': d->password = arg; break;
	case 'l': d->locale = arg; break;
	case 'z': d->zone = arg; break;
	case 'k': d->packages = arg; break;
	case 's': d->services = arg; break;
	case 999: d->skipPacstrap = TRUE; break;
	case 998: d->writeExt4 = TRUE; d->newFSLabel = arg; break;
	case 997: d->killfifo = arg; break;
	case 996: d->postcmds = g_list_append(d->postcmds, arg); break;
	case 995:
	{
		Repo *r = parse_repo_string(arg);
		if(!r)
		{
			printf("Invalid repo specified: %s\n", arg);
			return EINVAL;
		}
		d->repos = g_list_prepend(d->repos, r);
		break;
	}
	default: g_free(arg); return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

#define println(fmt, ...) printf(fmt "\n", __VA_ARGS__)

static inline void progress(Data *d)
{
	println("PROGRESS %f", (gfloat)(d)->steps / kMaxSteps);
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
#define RUN(d, process, sout, args...) 0; { \
	gchar *argv [] = {args, NULL}; \
	status = run(d, process, sout, (const gchar * const *)argv); \
}

// Returns the process's exit code as a negative, to distinugish a child
// process error (possibly not fatal) from a GSubprocess error (fatal).
// If stdout is non-NULL, then process must also be non-NULL, and the caller
// must call g_object_unref on process after using the stdout stream.
static gint run(Data *d, GSubprocess **process, GInputStream **sout, const gchar * const *args)
{
	if(killing)
		return 1;
	
	if(process) *process = NULL;
	if(sout) *sout = NULL;
	
	gchar *cmd = g_strjoinv(" ", (gchar **)args);
	println("Running: %s", cmd);
	g_free(cmd);
	
	GError *error = NULL;
	GSubprocess *proc = g_subprocess_newv(args, sout ? (G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE) : G_SUBPROCESS_FLAGS_NONE, &error);
	
	if(error)
		EXIT(d, 1, g_error_free(error), "%s", error->message)
	
	if(!g_subprocess_wait(proc, d->cancellable, &error) || error)
	{
		if(error)
			EXIT(d, 1, g_object_unref(proc);g_error_free(error), "%s", error->message)
		EXIT(d, 1, , "Process cancelled")
	}
	
	int exit = 1;
	if(g_subprocess_get_if_exited(proc))
		exit = g_subprocess_get_exit_status(proc);
	else
		println("Process aborted (signal: %i)", g_subprocess_get_if_signaled(proc) ? g_subprocess_get_term_sig(proc) : 0);
	
	if(sout)
		*sout = g_subprocess_get_stdout_pipe(proc);
	if(process)
		*process = proc;
	else
		g_object_unref(proc);
	return (-exit);
}

// Checks if the arg is available (non-NULL)
// If it isn't, reads and parses STDIN until it is non-NULL
static void ensure_argument(Data *d, gchar **arg, const gchar *argname)
{
	if(*arg == NULL)
		println("WAITING %s", argname);
	GIOChannel *ch = NULL; 
	while(*arg == NULL)
	{
		gchar *line = NULL;
		gsize len = 0;
		//char *line = NULL;
		//size_t size;
		//if(getline(&line, &size, stdin) == -1)
		//	exit(1);
		//g_message("got line");
		if(!ch) ch = g_io_channel_unix_new(STDIN_FILENO);
		if(g_io_channel_read_line(ch, &line, &len, NULL, NULL) != G_IO_STATUS_NORMAL)
			exit(1);
		if(!line)
			continue;
		
		// +2 because g_io_channel_read_line includes the \n at the end
		#define TRY(x, n) if(len >= ((n)+2) && !d->x && strncmp(line, #x "=", (n)+1) == 0) { d->x = g_strstrip(g_strndup(line+((n)+1), len-((n)+2))); }
		TRY(password, 8)
		else TRY(dest, 4)
		else TRY(hostname, 8)
		else TRY(username, 8)
		else TRY(name, 4)
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

static gint start(Data *d)
{
	// TODO: Check for internet connection before continuing installer.

	// Get the PARTUUID of the destination drive before
	// anything else. If anything it helps validate that
	// it's a real drive. Also I'd rather the install fail
	// right at the beginning than after pacstrap if
	// something's wrong with udev.
	ensure_argument(d, &d->dest, "dest");
	
	struct udev *udev = udev_new();
	if(!udev)
		EXIT(d, 1, , "udev unavailable", 1)
	
	// Apparently no way to get a udev_device by its devnode
	struct udev_enumerate *enumerate = udev_enumerate_new(udev);
	udev_enumerate_add_match_subsystem(enumerate, "block");
	udev_enumerate_scan_devices(enumerate);

	struct udev_device *dev = NULL;
	struct udev_list_entry *devListEntry;
	struct udev_list_entry *devices = udev_enumerate_get_list_entry(enumerate);
	udev_list_entry_foreach(devListEntry, devices)
	{
		const char *path = udev_list_entry_get_name(devListEntry);
		dev = udev_device_new_from_syspath(udev, path);
		if(g_strcmp0(udev_device_get_devnode(dev), d->dest) == 0)
			break;
		udev_device_unref(dev);
		dev = NULL;
	}
	
	if(!dev)
		EXIT(d, 1, udev_unref(udev), "Destination device not found.", 1)
	
	d->partuuid = g_strdup(udev_device_get_property_value(dev, "ID_PART_ENTRY_UUID"));
	if(!d->partuuid)
		EXIT(d, 1, udev_unref(udev), "PARTUUID not found.", 1)
	d->ofstype = g_strdup(udev_device_get_property_value(dev, "ID_FS_TYPE"));
	udev_unref(udev);
	
	return run_ext4(d);
}

static gint run_ext4(Data *d)
{
	if(!d->writeExt4)
	{
		step(d);
		return mount_volume(d);
	}
	
	ensure_argument(d, &d->dest, "dest");
	
	gint status = RUN(d, NULL, NULL, "udisksctl", "unmount", "-b", d->dest);
	// Don't worry if this fails, since it might not have been mounted at all
	//if(status > 0)
	//	return status;
	//else if(status < 0)
	//	EXIT(d, -status, , "Unmount failed with code %i.", -status)
	
	status = RUN(d, NULL, NULL, "mkfs.ext4", d->dest);
	if(status > 0)
		return status;
	else if(status < 0)
		EXIT(d, -status, , "mkfs.ext4 failed with code %i.", -status)
	
	if(d->newFSLabel)
	{
		status = RUN(d, NULL, NULL, "e2label", d->dest, d->newFSLabel);
		if(status > 0)
			return status;
		else if(status < 0)
			EXIT(d, -status, , "e2label failed with code %i.", -status)
	}
	
	step(d);
	return mount_volume(d);
}

static gint mount_volume(Data *d)
{
	ensure_argument(d, &d->dest, "dest");
	
	// Let udisks do the mounting, since it mounts the drive in a
	// unique spot, unlike simply mounting at /mnt.
	// Could do this with udisks dbus API but lazy.
	
	GInputStream *sout = NULL;
	GSubprocess *proc = NULL;
	gint status = RUN(d, &proc, &sout, "udisksctl", "mount", "-b", d->dest);
	if(status > 0)
		return status;
	status = -status;
	gchar buf[1024];
	gsize num = g_input_stream_read(sout, buf, 1024, NULL, NULL);
	g_object_unref(proc);
	
	gboolean alreadyMounted = FALSE;
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
		alreadyMounted = TRUE;
	}
	
	println("Mounted at %s", d->mountPath);
	step(d);
	gint r = run_pacstrap(d);
	if(!alreadyMounted)
	{
		printf("Unmounting volume\n");
		status = RUN(d, NULL, NULL, "udisksctl", "unmount", "-b", d->dest);
	}
	return r;
}

static gboolean search_file_for_line(FILE *file, const char *search)
{
	fseek(file, 0, SEEK_SET);

	// getline updates line and length from
	// their initial size if necessary.
	size_t mlength = 100;
	char *line = malloc(mlength);
	while(getline(&line, &mlength, file) >= 0)
	{
		char *tmp = line;
		size_t tmplength = strlen(tmp);
		// Set tmp to the string with no leading/trailing whitespace
		while(tmp[0] == ' ')
			++tmp, --tmplength;
		while(tmp[tmplength-1] == ' ' || tmp[tmplength-1] == '\n')
			--tmplength;
		tmp[tmplength] = '\0';
		
		if(g_strcmp0(tmp, search) == 0)
			return TRUE;
	}
	return FALSE;
}

static gint run_pacstrap(Data *d)
{
	// Install base first before user packages. That way we can modify
	// pacman.conf's repository list and download signing keys.
	if(!d->skipPacstrap)
	{
		gint status = RUN(d, NULL, NULL, "pacstrap", d->mountPath, "base");
		if(status > 0)
			return status;
		else if(status < 0)
			EXIT(d, -status, , "pacstrap failed with code %i.", -status)
	}

	char *confpath = g_build_path("/", d->mountPath, "etc", "pacman.conf", NULL);
	char *gpgdir = g_build_path("/", d->mountPath, "etc", "pacman.d", "gnupg", NULL);
	char *cachedir = g_build_path("/", d->mountPath, "var", "cache", "pacman", "pkg", NULL);
	
	if(d->repos)
	{
		printf("Adding repos to %s\n", confpath);
		FILE *conf = fopen(confpath, "a+");
		
		for(GList *it=d->repos; it!=NULL; it=it->next)
		{
			Repo *repo = it->data;
			println("Adding repo %s...", repo->name);
			
			// Don't add the repo if it's already there
			char *searchFor = g_strdup_printf("[%s]", repo->name);
			gboolean found = search_file_for_line(conf, searchFor);
			g_free(searchFor);
			
			if(!found)
			{
				fprintf(conf, "\n[%s]\nSigLevel = %s\nServer = %s\n",
					repo->name,
					repo->siglevel,
					repo->server);
			}
			
			println("Downloading signing keys for %s...", repo->name);
			guint nkeys = g_strv_length(repo->keys);
			g_message("n keys: %i", nkeys);
			char **args = g_new(char *, nkeys+7);
			args[0] = "pacman-key";
			args[1] = "--keyserver";
			args[2] = "pgp.mit.edu";
			args[3] = "--gpgdir";
			args[4] = gpgdir;
			args[5] = "--recv-keys";
			for(guint i=0;i<nkeys;++i)
				args[6+i] = repo->keys[i];
			args[nkeys+6] = NULL;
			for(guint x=0;x<nkeys+6;++x)
				g_message("%s", args[x]);
			gint status = run(d, NULL, NULL, (const gchar * const *)args);
			g_free(args);
			
			if(status > 0)
				return status;
			else if(status < 0)
				EXIT(d, -status, , "pacman-key failed with code %i.", -status)
		}
		
		fclose(conf);
	}
	
	if(d->skipPacstrap)
	{
		g_free(confpath);
		g_free(gpgdir);
		g_free(cachedir);
		step(d);
		return run_genfstab(d);
	}
	
	ensure_argument(d, &d->packages, "packages");
	char ** split = g_strsplit(d->packages, " ", -1);
	guint numPackages = 0;
	for(guint i=0;split[i]!=NULL;++i)
		if(split[i][0] != '\0') // two spaces between packages create empty splits
			numPackages++;
	
	char **args = g_new(gchar *, numPackages + 12);
	args[0] = "pacman";
	args[1] = "--noconfirm";
	args[2] = "--root";
	args[3] = d->mountPath;
	args[4] = "--cachedir";
	args[5] = cachedir;
	args[6] = "--config";
	args[7] = confpath;
	args[8] = "--gpgdir";
	args[9] = gpgdir;
	args[10] = "-Syu";
	for(guint i=0,j=11;split[i]!=NULL;++i)
	{
		if(split[i][0] != '\0')
		{
			if(g_strcmp0(split[i], "sudo") == 0)
				d->enableSudoWheel = TRUE;
			args[j++] = split[i];
		}
	}
	args[numPackages+11] = '\0';
		
	gint status = run(d, NULL, NULL, (const gchar * const *)args);
	g_free(args);
	g_strfreev(split);
	g_free(confpath);
	g_free(gpgdir);
	g_free(cachedir);
	
	if(status > 0)
		return status;
	else if(status < 0)
		EXIT(d, -status, , "pacman failed with code %i.", -status)
	
	step(d);
	return run_genfstab(d);
}

static gint run_genfstab(Data *d)
{
	gchar *etcpath = g_build_path("/", d->mountPath, "etc", NULL);
	errno = 0;
	if(mkdir(etcpath, 0755))
		if(errno != EEXIST)
			EXIT(d, errno, g_free(etcpath), "Failed to create /etc");
	
	gchar *fstabpath = g_strconcat(etcpath, "/fstab", NULL);
	g_free(etcpath);
	
	println("Writing generated fstab to %s", fstabpath);
	FILE *fstab = fopen(fstabpath, "w");
	g_free(fstabpath);
	
	if(!fstab)
		EXIT(d, 1, , "Failed to open fstab for writing")

	// Genfstab doesn't always write what we want (for example,
	// writing nosuid under options when installing to a flash drive)
	// so write it outselves.

	const gchar *fstype = d->writeExt4 ? "ext4" : d->ofstype;
	if(!fstype) // Should never happen, since the drive has already been mounted
		EXIT(d, 1, fclose(fstab), "Unknown filesystem type")

	fprintf(fstab, "# <file system>\t<mount point>\t<fs type>\t<options>\t<dump>\t<pass>\n\n");
	fprintf(fstab, "PARTUUID=%s\t/\t%s\trw,relatime,data=ordered\t0\t1\n",
		d->partuuid,
		fstype);
	
	fclose(fstab);
	
	step(d);
	return run_chroot(d);
}

static gint run_chroot(Data *d)
{
	println("Changing root to %s", d->mountPath);
	if(!exitable_chroot(d->mountPath))
		EXIT(d, 1, , "Chroot failed (must run as root).", 1)
	
	// A number of processes use devfs (such as piping to/from
	// /dev/null, and hwclock which uses /dev/rtc)
	if(mount("udev", "/dev", "devtmpfs", MS_NOSUID, "mode=0755"))
	{
		// Don't error if it's already mounted
		if(errno != EBUSY)
			EXIT(d, errno, , "Failed to mount /dev devtmps with error %i.", errno)
	}
	
	step(d);
	gint r = set_passwd(d);
	printf("Leaving chroot\n");
	umount("/dev");
	exitable_chroot(NULL);
	return r;
}

static gint chpasswd(Data *d, const gchar *user, const gchar *password)
{
	println("Running chpasswd on %s", user);
	GError *error = NULL;
	GSubprocess *proc = g_subprocess_new(G_SUBPROCESS_FLAGS_STDIN_PIPE, &error, "chpasswd", NULL);
	if(error)
		EXIT(d, 1, g_error_free(error), "%s", error->message)
	
	GOutputStream *stdin = g_subprocess_get_stdin_pipe(proc);

	int userlen = strlen(user);
	int pwdlen = strlen(password);
	
	if(g_output_stream_write(stdin, user, userlen, d->cancellable, &error) != userlen 
	|| g_output_stream_write(stdin, ":", 1, d->cancellable, &error) != 1
	|| g_output_stream_write(stdin, password, pwdlen, d->cancellable, &error) != pwdlen
	|| !g_output_stream_close(stdin, d->cancellable, &error))
	{
		EXIT(d, 1, g_object_unref(proc);g_clear_error(&error), "%s", (error ? error->message : "Failed to write to chpasswd"))
	}
	
	if(!g_subprocess_wait(proc, d->cancellable, &error) || error)
	{
		if(error)
			EXIT(d, 1, g_object_unref(proc);g_error_free(error), "%s", error->message)
		EXIT(d, 1, , "Process cancelled")
	}
	
	int exit = g_subprocess_get_exit_status(proc);
	g_object_unref(proc);
	if(exit != 0)
		EXIT(d, exit, , "chpasswd failed with code %i.", exit)
	return 0;
}

static gint set_passwd(Data *d)
{
	ensure_argument(d, &d->password, "password");
	if(d->password[0] == '\0')
	{
		printf("Skipping set password\n");
		step(d);
		return set_locale(d);
	}
	
	gint status = 0;
	if((status = chpasswd(d, "root", d->password)))
		return status;
	
	step(d);
	return set_locale(d);
}

static gint set_locale(Data *d)
{
	ensure_argument(d, &d->locale, "locale");
	
	const gchar *locale = d->locale;
	if(locale[0] == '\0')
		locale = "en_US.UTF-8";
	gchar *localeesc = g_regex_escape_string(locale, -1);
	
	// Remove comments from any lines matching the given locale prefix
	gchar *pattern = g_strdup_printf("s/^#(%s.*$)/\\1/", localeesc);
	gint status = RUN(d, NULL, NULL, "sed", "-i", "-E", pattern, "/etc/locale.gen");
	g_free(pattern);
	if(status > 0)
	{
		g_free(localeesc);
		return status;
	}
	else if(status < 0)
		EXIT(d, -status, g_free(localeesc), "Edit of /etc/locale.gen failed with code %i.", -status)
	
	// Write first locale match to /etc/locale.conf (for the LANG variable)
	
	GInputStream *sout = NULL;
	GSubprocess *proc = NULL;
	pattern = g_strdup_printf("^%s", localeesc);
	g_free(localeesc);
	status = RUN(d, &proc, &sout, "grep", "-m1", "-e", pattern, "/etc/locale.gen");
	g_free(pattern);
	if(status > 0)
		return status;
	else if(status < -1) // exit code of 1 means no lines found, not error
		EXIT(d, -status, g_object_unref(proc), "grep failed with code %i.", -status)
	
	gint lconff = open("/etc/locale.conf", O_WRONLY|O_CREAT);
	if(lconff < 0)
		EXIT(d, errno, g_object_unref(proc), "Failed to open locale.conf for writing")
	
	write(lconff, "LANG=", 5);
	
	char buf[1024];
	gsize num = 0;
	while((num = g_input_stream_read(sout, buf, 1024, NULL, NULL)) > 0)
	{
		// Only write until the first space
		const gchar *space = strchr(buf, ' ');
		if(space != NULL)
			num = (space - buf);

		if(write(lconff, buf, num) != num)
			EXIT(d, 1, close(lconff);g_object_unref(proc), "Failed to write %i bytes to locale.conf", num)
		
		if(space != NULL)
			break;
	}	
	write(lconff, "\n", 1);
	close(lconff);

	// Run locale-gen
	status = RUN(d, NULL, NULL, "locale-gen");
	if(status > 0)
		return status;
	else if(status < 0)
		EXIT(d, -status, , "locale-gen failed with code %i.", -status)
	
	step(d);
	return set_zone(d);
}

static gint set_zone(Data *d)
{
	ensure_argument(d, &d->zone, "zone");
	const gchar *zone = "UTC";
	if(d->zone[0] != '\0')
		zone = d->zone;
	
	gchar *path = g_build_path("/", "/usr/share/zoneinfo/", zone, NULL);
	println("Symlinking %s to /etc/localtime", path);

	errno = 0;
	if(symlink(path, "/etc/localtime"))
	{
		// Try to symlink first before deleting the file
		// If unlink first, then symlink, and symlink fails, then the
		// install is left with no /etc/locatime at all. Checking that the
		// symlink error is EEXIST, and not some other filesystem
		// error, means it will probably succeed after calling unlink.
		if(errno == EEXIST)
		{
			errno = 0;
			printf("/etc/localtime already exists, replacing\n");
			if(!unlink("/etc/localtime"))
				symlink(path, "/etc/localtime");
		}
	}
	
	if(errno)
		EXIT(d, errno, g_free(path), "Error symlinking: %i", errno);
	
	step(d);
	g_free(path);
	
	// Set /etc/adjtime
	gint status = RUN(d, NULL, NULL, "hwclock", "--systohc");
	if(status > 0)
		return status;
	else if(status < 0)
		EXIT(d, -status, , "Failed to set system clock with error %i.", -status)
	
	step(d);
	return set_hostname(d);
}

static gint set_hostname(Data *d)
{
	ensure_argument(d, &d->hostname, "hostname");
	if(d->hostname[0] == '\0')
	{
		printf("Skipping setting hostname\n");
		step(d);
		return create_user(d);
	}
	
	println("Writing %s to hostname", d->hostname);
	gint hostf = open("/etc/hostname", O_WRONLY|O_CREAT);
	if(hostf < 0)
		EXIT(d, errno, , "Failed to open /etc/hostname for writing")
	int len = strlen(d->hostname);
	write(hostf, d->hostname, len);
	write(hostf, "\n", 1);
	close(hostf);
	
	step(d);
	return create_user(d);
}

static gint create_user(Data *d)
{
	ensure_argument(d, &d->username, "username");
	if(d->username[0] == '\0')
	{
		printf("Skipping create user\n");
		d->steps++; // create_user has two steps
		step(d);
		return enable_services(d);
	}
	
	gint status = RUN(d, NULL, NULL, "useradd", "-m", "-G", "wheel", d->username);
	
	if(status > 0)
		return status;
	// error code 9 is user already existed. As this installer should be
	// repeatable (in order to easily fix problems and retry), ignore this error.
	else if(status != -9 && status < 0)
		EXIT(d, -status, , "Failed to create user, error code %i.", -status)
	
	ensure_argument(d, &d->password, "password");
	
	if(d->password[0] == '\0')
	{
		printf("Skipping set password on user\n");
	}
	else
	{
		status = 0;
		if((status = chpasswd(d, d->username, d->password)))
			return status;
	}

	ensure_argument(d, &d->name, "name");

	if(d->name[0] == '\0')
	{
		printf("Skipping set real name on user\n");
	}
	else
	{
		gint status = RUN(d, NULL, NULL, "chfn", "-f", d->name, d->username);
		
		if(status > 0)
			return status;
		else if(status < 0)
			EXIT(d, -status, , "Failed to create user, error code %i.", -status)
	}
	
	step(d);
	
	// Enable sudo for user
	if(d->enableSudoWheel)
	{
		println("Enabling sudo for user %s", d->username);
		gint status = RUN(d, NULL, NULL, "sed", "-i", "-E", "s/#\\s?(%wheel ALL=\\(ALL\\) ALL)/\\1/", "/etc/sudoers");
		if(status > 0)
			return status;
		else if(status < 0)
			EXIT(d, -status, , "Edit of /etc/sudoers failed with code %i.", -status)
	}
	
	step(d);
	return enable_services(d);
}

static gint enable_services(Data *d)
{
	ensure_argument(d, &d->services, "services");
	if(d->services[0] == '\0')
	{
		printf("No services to enable\n");
		step(d);
		return run_postcmd(d);
	}
	
	gchar ** split = g_strsplit(d->services, " ", -1);
	guint numServices = 0;
	for(guint i=0;split[i]!=NULL;++i)
		if(split[i][0] != '\0') // two spaces between services create empty splits
			numServices++;
	
	gchar **args = g_new(gchar *, numServices + 3);
	args[0] = "systemctl";
	args[1] = "enable";
	for(guint i=0,j=2;split[i]!=NULL;++i)
		if(split[i][0] != '\0')
			args[j++] = split[i];
	args[numServices+2] = '\0';
		
	gint status = run(d, NULL, NULL, (const gchar * const *)args);
	g_free(args);
	g_strfreev(split);
	
	if(status > 0)
		return status;
	else if(status < 0)
		EXIT(d, -status, , "systemctl enable failed with code %i.", -status)
	
	step(d);
	return run_postcmd(d);
}

static gint run_postcmd(Data *d)
{
	if(d->postcmds == NULL)
	{
		printf("No postcmds\n");
		step(d);
		return 0;
	}
	
	for(GList *it=d->postcmds; it!=NULL; it=it->next)
	{
		gint status = RUN(d, NULL, NULL, "/bin/sh", "-c", it->data);
		
		if(status > 0)
			return status;
		else if(status < 0)
			EXIT(d, -status, , "Postcmd '%s' failed with code %i.", it->data, -status)
	}
	
	step(d);
	return 0;
}
