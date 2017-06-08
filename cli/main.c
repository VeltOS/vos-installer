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
	{0}
};

const char *argp_program_version = "vos-install-cli 0.1";
const char *argp_program_bug_address = "Aidan Shafran <zelbrium@gmail.com>";
static char argp_program_doc[] = "An installer for VeltOS (Arch Linux). See top of main.c for detailed instructions on how to use the installer. The program author is not responsible for any damages, including but not limited to exploded computer, caused by this program. Use as root and with caution.";

const guint kMaxSteps = 12;

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
	
	// Running data
	GCancellable *cancellable;
	guint steps;
	gchar *mountPath;
	gboolean enableSudoWheel;
} Data;

static error_t parse_arg(int key, char *arg, struct argp_state *state);
static void progress(Data *d);
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
			EXIT(d, 1, g_object_unref(proc);g_error_free(error), "%s", error->message)
		EXIT(d, 1, , "Process cancelled")
	}
	
	int exit = 1;
	if(g_subprocess_get_if_exited(proc))
		exit = g_subprocess_get_exit_status(proc);
	else
		println("Process aborted (signal: %i)", g_subprocess_get_if_signaled(proc) ? g_subprocess_get_term_sig(proc) : 0);
	
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
	//goto skip;
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
	{
		if(split[i][0] != '\0')
		{
			if(g_strcmp0(split[i], "sudo") == 0)
				d->enableSudoWheel = TRUE;
			args[j++] = split[i];
		}
	}
	args[numPackages+3] = '\0';
		
	gint status = run(d, NULL, NULL, (const gchar * const *)args);
	g_free(args);
	g_strfreev(split);
	
	if(status > 0)
		return status;
	else if(status < 0)
		EXIT(d, -status, , "pacstrap failed with code %i.", -status)
	
skip:
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
		EXIT(d, -status, g_object_unref(proc), "Genfstab failed with code %i.", -status)
	
	gchar *path = g_build_path("/", d->mountPath, "etc/fstab", NULL);
	println("Writing generated fstab to %s", path);
	gint fstab = open(path, O_WRONLY|O_CREAT);
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
	umount("/dev");
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
	ensure_argument(d, &d->password);
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
	ensure_argument(d, &d->locale);
	
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
	
	GInputStream *stdout = NULL;
	GSubprocess *proc = NULL;
	pattern = g_strdup_printf("^%s", localeesc);
	g_free(localeesc);
	status = RUN(d, &proc, &stdout, "grep", "-m1", "-e", pattern, "/etc/locale.gen");
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
	while((num = g_input_stream_read(stdout, buf, 1024, NULL, NULL)) > 0)
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
	
	step(d);
	return set_zone(d);
}

static gint set_zone(Data *d)
{
	ensure_argument(d, &d->zone);
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
	ensure_argument(d, &d->hostname);
	if(d->hostname[0] == '\0')
	{
		printf("Skipping setting hostname");
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
	ensure_argument(d, &d->username);
	if(d->username[0] == '\0')
	{
		printf("Skipping create user\n");
		d->steps++; // create_user has two steps
		step(d);
		return enable_services(d);
	}
	
	ensure_argument(d, &d->password);
	
	gint status = RUN(d, NULL, NULL, "useradd", "-m", "-G", "wheel", d->username);
	
	if(status > 0)
		return status;
	// error code 9 is user already existed. As this installer should be
	// repeatable (in order to easily fix problems and retry), ignore this error.
	else if(status != -9 && status < 0)
		EXIT(d, -status, , "Failed to create user, error code %i.", -status)
	
	status = 0;
	if((status = chpasswd(d, d->username, d->password)))
		return status;
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
	ensure_argument(d, &d->services);
	if(d->services[0] == '\0')
	{
		printf("No services to enable\n");
		step(d);
		return 0;
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
	return 0;
}
