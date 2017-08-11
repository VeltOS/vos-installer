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
 *     --refind   Install the rEFInd boot manager. This is a UEFI-only
 *                   boot manager. The install updates your UEFI NVRAM
 *                   to make itself the default boot. rEFInd auto-detects
 *                   Linux kernels, so no configuration should be needed.
 *                   If no device is specified, rEFInd installs to its
 *                   automatic location. Otherwise, the installer will
 *                   determine if the drive is internal or external and
 *                   choose to use refind-install's --root or --usedefault
 *                   flags, respectively.
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

#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <argp.h>
#include <libudev.h>
#include <stdbool.h>
#include <stdint.h>
#include <glib.h>

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
	char *dest;
	char *hostname;
	char *username;
	char *name;
	char *password;
	char *locale;
	char *zone;
	char *packages;
	char *services;
	bool skipPacstrap;
	bool writeExt4;
	bool debug;
	char *newFSLabel; // Only if writeExt4
	bool refind;
	char *refindDest;
	GList *postcmds;
	GList *repos;
	
	// Running data
	size_t steps;
	char *mountPath;
	bool enableSudoWheel;
	char *killfifo;
	char *partuuid;
	char *ofstype; // original fs type before running mkfs.ext4, or NULL if none
	bool refindExternal; // Set true if refind is being installed on an external device
	
	int selfpipe[2];
	bool killing;
} Data;

static error_t parse_arg(int key, char *arg, struct argp_state *state);
static Repo * parse_repo_string(const char *arg);
static void * thread_watch_killfifo(void *data);
static void * thread_watch_parent(void *data);
static void on_signal(int sig, siginfo_t *siginfo, void *context);
static void free_repo_struct(Repo *r);
static void step(Data *d);
static int run(int *out, const char * const *args);
static int run_shell(int *out, const char *command);
static void ensure_argument(Data *d, char **arg, const char *argname);
static int start(Data *d);
static int run_ext4(Data *d);
static int mount_volume(Data *d);
static int run_pacstrap(Data *d);
static int run_genfstab(Data *d);
static int run_chroot(Data *d);
static int set_passwd(Data *d);
static int set_locale(Data *d);
static int set_zone(Data *d);
static int set_hostname(Data *d);
static int create_user(Data *d);
static int enable_services(Data *d);
static int run_postcmd(Data *d);
static int install_refind(Data *d);

#define println(fmt...) { printf(fmt); printf("\n"); }

#define NONBLOCK(fd) fcntl(fd,F_SETFL,fcntl(fd,F_GETFL)|O_NONBLOCK)

#define FAIL(code, x, fmt...) { \
	println(fmt); \
	x; \
	return code; \
}

// Use like: int status = RUN(..)
#define RUN(out, args...) 0; { \
	char *argv [] = {args, NULL}; \
	status = run(out, (const char * const *)argv); \
}

static struct argp_option options[] =
{
	{"dest",      'd', "block device", 0, "The volume to install Arch at", 0},
	{"hostname",  'h', "name",      0, "Machine hostname", 0},
	{"username",  'u', "name",      0, "Default account username", 0},
	{"name",      'n', "name",      0, "Real name of default user", 0},
	{"password",  'p', "password",  0, "Root/default account password", 0},
	{"locale",    'l', "locale",    0, "Locale (locale.gen format)", 0},
	{"zone",      'z', "file",      0, "Timezone file (relative to /usr/share/zoneinfo/)", 0},
	{"packages",  'k', "packages",  0, "List of extra packages separated by spaces", 0},
	{"services",  's', "services",  0, "List of systemd services to enable", 0},
	{"ext4",      998, "new filesystem label",           OPTION_ARG_OPTIONAL, "Erases the destination volume and writes a new ext4 filesystem. Optionally specify a parameter which will become the new filesystem label.", 0},
	{"skippacstrap", 999, 0,        0, "Skips pacstrap, to avoid reinstalling all packages if they're already installed", 0},
	{"kill",      997, "kill",           0, "Optionally specify the path to a fifo. If any data is received at this fifo, the install will be aborted immediately.", 0},
	{"postcmd",   996, "postcmd",     0, "Optionally specify a shell command to run after installation. This may be specified multiple times.", 0},
	{"repo",      995, "repo",      0, "Specify a pacman repository to add to /etc/pacman.conf on the target machine, in the format \"Name,Server,SigLevel,Keys...\" where keys are full PGP fingerprints to download public keys to add to pacman's keyring.", 0},
	{"debug",     994, 0,      0, "specify to enable debug mode", 0},
	{"refind",    993, "block device",      OPTION_ARG_OPTIONAL, "Install rEFInd boot manager to the default EFI partition. Optionally specify a partition to perform a more compatible install (good for external devices).", 0},
	{0}
};

const char *argp_program_version = "vos-install-cli 0.1";
const char *argp_program_bug_address = "Aidan Shafran <zelbrium@gmail.com>";
static char argp_program_doc[] = "An installer for VeltOS (Arch Linux). See top of main.c for detailed instructions on how to use the installer. The program author is not responsible for any damages, including but not limited to exploded computer, caused by this program. Use as root and with caution.";

static const size_t kMaxSteps = 17;
static Data *d;


int main(int argc, char **argv)
{
	int code;
	
	// Don't buffer stdout, seems to mess with GSubprocess/GInputStream
	setbuf(stdout, NULL);

	// Parse arguments
	d = g_new0(Data, 1);
	static struct argp argp = {options, parse_arg, NULL, argp_program_doc, NULL, NULL, NULL};
	error_t error;
	if((error = argp_parse(&argp, argc, argv, 0, 0, d)))
	{
		println("Invalid arguments (%i)", error);
		code = 1;
		goto exit;
	}
	
	// Check if any command line arguments are NONE and replace them
	// with "" so that ensure_argument knows they have been set
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

	// Setup selfpipe
	// Nonblocking because a blocking write() in the signal handler,
	// waiting for a read in the main thread = deadlock
	errno = 0;
	if(pipe(d->selfpipe)
	|| NONBLOCK(d->selfpipe[0]) == -1
	|| NONBLOCK(d->selfpipe[1]) == -1)
	{
		println("Error creating selfpipe (%i)", errno);
		code = 1;
		goto exit;
	}

	// Watch for stop signals
	sigset_t bsigs;
	sigemptyset(&bsigs);
	sigaddset(&bsigs, SIGINT);
	sigaddset(&bsigs, SIGTERM);
	sigaddset(&bsigs, SIGHUP);
	sigaddset(&bsigs, SIGCHLD);
	
	struct sigaction act = {0};
	act.sa_sigaction = on_signal;
	act.sa_flags = SA_SIGINFO;
	act.sa_mask = bsigs;
	if(sigaction(SIGINT, &act, NULL)
	|| sigaction(SIGTERM, &act, NULL)
	|| sigaction(SIGHUP, &act, NULL)
	|| sigaction(SIGCHLD, &act, NULL))
	{
		println("Failed to setup signal handlers! Aborting just to be safe.");
		code = 1;
		goto exit;
	}

	// According to POSIX (see pthread(7) man page), when a signal
	// arrives at a process, a thread is arbitrarily selected to
	// receive the signal. It appears that on Linux, that is always
	// the program's main thread. But it doesn't have to be, so to
	// make sure, temporarily block the signals we're interested in,
	// so that the other threads inherit the block mask. Then, after
	// spawning the threads, unblock the signals on this thread.
	pthread_sigmask(SIG_BLOCK, &bsigs, NULL);
	
	// Start threads to monitor for abort conditions
	pthread_t p, p2;
	if(d->killfifo)
		pthread_create(&p, NULL, thread_watch_killfifo, NULL);
	pthread_create(&p2, NULL, thread_watch_parent, NULL);
	
	// Unblock blocked signals
	pthread_sigmask(SIG_UNBLOCK, &bsigs, NULL);

	// Begin installation
	code = start(d);

exit:
	g_free(d->dest);
	g_free(d->hostname);
	g_free(d->username);
	g_free(d->name);
	g_free(d->password);
	g_free(d->locale);
	g_free(d->zone);
	g_free(d->packages);
	g_free(d->services);
	g_free(d->mountPath);
	g_list_free_full(d->postcmds, g_free);
	g_list_free_full(d->repos, (GDestroyNotify)free_repo_struct);
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
	case 'n': d->name = arg; break;
	case 'p': d->password = arg; break;
	case 'l': d->locale = arg; break;
	case 'z': d->zone = arg; break;
	case 'k': d->packages = arg; break;
	case 's': d->services = arg; break;
	case 999: d->skipPacstrap = true; break;
	case 998: d->writeExt4 = true; d->newFSLabel = arg; break;
	case 997: d->killfifo = arg; break;
	case 996: d->postcmds = g_list_append(d->postcmds, arg); break;
	case 995:
	{
		Repo *r = parse_repo_string(arg);
		if(!r)
		{
			println("Invalid repo specified: %s", arg);
			return EINVAL;
		}
		d->repos = g_list_prepend(d->repos, r);
		break;
	}
	case 994: d->debug = TRUE; break;
	case 993: d->refind = true; d->refindDest = arg; break;
	default: g_free(arg); return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

static Repo * parse_repo_string(const char *arg)
{
	// 0,    1,      2,        3...
	// name, server, siglevel, keys...
	
	char **split = g_strsplit(arg, ",", -1);
	size_t length = g_strv_length(split);
	size_t numkeys = length - 3;
	
	if(length < 3)
	{
		g_strfreev(split);
		return NULL;
	}
	
	// Make sure all keys specified are valid fingerprints
	// Also remove spaces and 0x if present
	for(size_t i=3; i<length; ++i)
	{
		char *key = g_strstrip(split[i]);
		size_t len = strlen(key);
		
		// Remove 0x if it's there
		if(len >= 2 && key[0] == '0' && (key[1] == 'x' || key[1] == 'X'))
		{
			// len+1 to move back NULL terminator too
			for(size_t k=2; k<len+1; ++k)
				key[k-2] = key[k];
			len -= 2;
		}
		
		for(size_t j=0; j<len; ++j)
		{
			if(key[j] == ' ')
			{
				for(size_t k=j+1; k<len+1; ++k)
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
	for(size_t i=0; i<numkeys; ++i)
		r->keys[i] = split[3+i];
	r->keys[numkeys] = NULL;
	
	g_free(split);
	return r;
}

// Wait for data to be sent to the killfifo
static void * thread_watch_killfifo(UNUSED void *data)
{
	int fifo = open(d->killfifo, O_RDONLY);
	char x[2];
	x[0] = '\0';
	x[1] = '\0';
	while(1)
	{
		read(fifo, &x, 1);
		kill(getpid(), SIGINT);
		// TODO: Clear and/or request user only send one char of data
	}
	return NULL;
}

// Constantly make sure the parent doesn't change (will
// happen if the original parent dies). If it does, abort.
static void * thread_watch_parent(UNUSED void *data)
{
	int ppid = getppid();
	do
	{
		sleep(1);
	} while(getppid() == ppid);
	println("\nParent changed, stopping install to be safe\n");
	kill(getpid(), SIGINT);
	return NULL;
}

static void on_signal(int sig, UNUSED siginfo_t *siginfo, UNUSED void *context)
{
	if(sig != SIGCHLD)
		d->killing = true;
	
	// To avoid some race conditions, also use a selfpipe to
	// inform of these signals.
	int savederr = errno;
	(void)write(d->selfpipe[1], "", 1);
	errno = savederr;
}

static void free_repo_struct(Repo *r)
{
	g_free(r->name);
	g_free(r->server);
	g_free(r->siglevel);
	g_strfreev(r->keys);
	g_free(r);
}

static void step(Data *d)
{
	d->steps++;
	println("PROGRESS %f", (gfloat)(d)->steps / kMaxSteps);
}

// Run a process. If an exit signal comes though, try to give the
// process a little bit of time to exit, and if it doesn't die in
// time, force kill it.
// Supply a integer to store the read end of a pipe if the child's
// output should be redirected to that pipe, or NULL to keep output
// at stdout. run still waits for the child to exit before returning
// even if a pipe is supplied.
// Returns the process's exit code as a negative, to distinugish a child
// process error (possibly not fatal) from a fork/abort error (fatal).
static int run_full(int *out, bool mute, const char * const *args)
{
	if(d->killing)
		FAIL(errno, , "Install aborted")
	
	if(d->debug || !mute)
	{
		char *cmd = g_strjoinv(" ", (char **)args);
		println("Running: %s", cmd);
		g_free(cmd);
	}

	if(d->debug)
	{
		printf("Continue? (y/n) ");
		char *line = NULL;
		size_t len = 0;
		if(getline(&line, &len, stdin) == -1)
			exit(1);
		if(g_strcmp0(line, "y\n") != 0)
			exit(1);
	}

	int fd[2];
	if(out)
	{
		*out = 0;
		if(pipe(fd) || NONBLOCK(fd[0]) || NONBLOCK(fd[1]))
			FAIL(errno, , "Failed to open pipe")
		*out = fd[0];
	}
	
	// Spawn new process
	errno = 0;
	pid_t ppid = getpid();
	pid_t pid = fork();
	
	if(pid == -1)
	{
		FAIL(errno, , "Failed to fork new process")
	}
	else if(pid == 0) // Child process
	{
		// Give the child its own process group
		setpgrp();
		
		// Redirect child's STDOUT/ERR to pipe if requested
		if(out)
		{
			close(fd[0]);
			dup2(fd[1], STDOUT_FILENO);
			dup2(fd[1], STDERR_FILENO);
		}
		
		// Child processes should be killed cleanly, but just incase
		// something bad happens (parent segfaults or SIGKILL'd), this
		// is a last resort to get the child to die.
		if(prctl(PR_SET_PDEATHSIG, SIGHUP))
			abort();
		// Prevent race condition of parent dying before prctl is called
		if(getppid() != ppid)
			abort();
		
		execvp(args[0], (char * const *)args);
		println("Error: Failed to launch process. It might not exist.");
		abort();
	}

	if(out)
		close(fd[1]);

	// Wait for process to exit, or something to go wrong
	// Loop to handle EINTR.
	int exitstatus = 0;
	while(1)
	{
		// This will wait for any signal, or exit immediately
		// if we should be aborting. This avoids a race condition
		// between checking d->killing and waitpid.
		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(d->selfpipe[0], &rfds);
		errno = 0;
		select(d->selfpipe[0]+1, &rfds, NULL, NULL, NULL);
		if(errno != 0 && errno != EINTR)
			d->killing = true;
		static char dummy[PIPE_BUF];
		while(read(d->selfpipe[0], dummy, sizeof(dummy)) > 0);
		
		if(!d->killing)
		{
			errno = 0;
			if(waitpid(pid, &exitstatus, WNOHANG) > 0)
				break;
			else if(errno == 0 || errno == EINTR) // 0 for WNOHANG
				continue;
		}
		
		// Something went wrong; stop the child process.
		kill(-getpgid(pid), SIGINT);
		
		println("Waiting 1s for child process to exit...");
		
		// Give the process time to cleanly exit. If it does,
		// SIGCHLD will interrupt the sleep.
		// If the user sends another interrupt during this time,
		// it will also exit the sleep. (Assume two interrupts =
		// they really want it dead)
		sleep(1);
		
		// Death
		kill(-getpgid(pid), SIGKILL);
		waitpid(pid, NULL, 0);
		
		if(d->killing)
			FAIL(1, , "Install aborted")
		else
			FAIL(errno, , "Error monitoring process")
	}

	int exit = 1;
	if(WIFEXITED(exitstatus))
		exit = WEXITSTATUS(exitstatus);
	else
		println("Process aborted (signal: %i)", WIFSIGNALED(exitstatus) ? WTERMSIG(exitstatus) : 0);
	return (-exit);
}

static int run(int *out, const char * const *args)
{
	return run_full(out, FALSE, args);
}

static int run_shell(int *out, const char *command)
{
	const char *args[] = {"sh", "-c", command, NULL};
	return run_full(out, TRUE, args);
}

// Checks if the arg is available (non-NULL)
// If it isn't, reads and parses STDIN until it is non-NULL
static void ensure_argument(Data *d, char **arg, const char *argname)
{
	if(*arg == NULL)
		println("WAITING %s", argname);
	while(*arg == NULL)
	{
		char *line = NULL;
		size_t len = 0;
		if(getline(&line, &len, stdin) == -1)
			exit(1);
		
		// +2 because getline includes the \n at the end
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
}

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
// Returns true on success. On failure, the working directory
// may be changed, but the chroot will not be.
static bool exitable_chroot(const char *path)
{
	static int originalRoot = -1;
	if(originalRoot < 0)
		originalRoot = open("/", O_RDONLY);
	if(originalRoot < 0)
		return false;
	
	int tmpcwd = open(".", O_RDONLY);
	if(tmpcwd < 0)
		return false;
	
	if((path && chdir(path)) || (!path && fchdir(originalRoot)))
	{
		close(tmpcwd);
		return false;
	}
	
	if(chroot("."))
	{
		// Try to move back to previous directory. This could fail,
		// in which case the cwd might change.
		fchdir(tmpcwd);
		close(tmpcwd);
		return false;
	}

	close(tmpcwd);
	if(!path)
	{
		close(originalRoot);
		originalRoot = -1;
	}
	return true;
}

static int start(Data *d)
{
	println("Checking internet connection...");
	
	if(run_shell(0, "ping -c1 8.8.8.8 &>/dev/null"))
	{
		println("\nPlease connect to the internet to continue the install.");
		if(run_shell(0, "until ping -c1 8.8.8.8 &>/dev/null; do sleep 1; done"))
			return 1;
	}
	
	println("Connection to Google DNS available.");

	// Get the PARTUUID of the destination drive before
	// anything else. If anything it helps validate that
	// it's a real drive. Also I'd rather the install fail
	// right at the beginning than after pacstrap if
	// something's wrong with udev.
	ensure_argument(d, &d->dest, "dest");
	
	struct udev *udev = udev_new();
	if(!udev)
		FAIL(1, , "udev unavailable")
	
	// Apparently no way to get a udev_device by its devnode
	struct udev_enumerate *enumerate = udev_enumerate_new(udev);
	udev_enumerate_add_match_subsystem(enumerate, "block");
	udev_enumerate_scan_devices(enumerate);

	struct udev_device *dev = NULL;
	struct udev_device *installdev = NULL;
	struct udev_device *refinddev = NULL;
	struct udev_list_entry *devListEntry;
	struct udev_list_entry *devices = udev_enumerate_get_list_entry(enumerate);
	udev_list_entry_foreach(devListEntry, devices)
	{
		const char *path = udev_list_entry_get_name(devListEntry);
		dev = udev_device_new_from_syspath(udev, path);
		if(!installdev && g_strcmp0(udev_device_get_devnode(dev), d->dest) == 0)
		{
			installdev = dev;
			udev_device_ref(dev);
		}
		if(d->refind
		&& !refinddev
		&& g_strcmp0(udev_device_get_devnode(dev), d->refindDest) == 0)
		{
			refinddev = dev;
			udev_device_ref(dev);
		}
		udev_device_unref(dev);
		dev = NULL;
		if(d->refind && installdev && refinddev)
			break;
		else if(!d->refind && installdev && refinddev)
			break;
	}
	
	if(!installdev)
		FAIL(1, udev_unref(udev), "Install destination device not found.")
	if(!refinddev)
		FAIL(1, udev_unref(udev), "rEFInd destination device not found.")
	
	if(g_strcmp0(udev_device_get_property_value(refinddev, "ID_FS_TYPE"), "vfat") != 0)
		FAIL(1, udev_unref(udev), "Given rEFInd destination device is not formatted as vfat.")

	struct udev_device *rfparent = udev_device_get_parent(refinddev);
	if(!rfparent)
		FAIL(1, udev_unref(udev), "rEFInd destination device (parent) not found.")
	
	const char *removable = udev_device_get_sysattr_value(rfparent, "removable");
	d->refindExternal = false;
	if(removable)
		d->refindExternal = strtol(removable, NULL, 10) ? 1 : 0;
	
	d->partuuid = g_strdup(udev_device_get_property_value(installdev, "ID_PART_ENTRY_UUID"));
	if(!d->partuuid)
		FAIL(1, udev_unref(udev), "PARTUUID not found.")
	d->ofstype = g_strdup(udev_device_get_property_value(installdev, "ID_FS_TYPE"));
	udev_unref(udev);
	
	return run_ext4(d);
}

static int run_ext4(Data *d)
{
	if(!d->writeExt4)
	{
		step(d);
		return mount_volume(d);
	}
	
	ensure_argument(d, &d->dest, "dest");
	
	int status = RUN(NULL, "udisksctl", "unmount", "-b", d->dest);
	// Don't worry if this fails, since it might not have been mounted at all
	//if(status > 0)
	//	return status;
	//else if(status < 0)
	//	FAIL(-status, , "Unmount failed with code %i.", -status)
	
	status = RUN(NULL, "mkfs.ext4", "-F", d->dest);
	if(status > 0)
		return status;
	else if(status < 0)
		FAIL(-status, , "mkfs.ext4 failed with code %i.", -status)
	
	if(d->newFSLabel)
	{
		status = RUN(NULL, "e2label", d->dest, d->newFSLabel);
		if(status > 0)
			return status;
		else if(status < 0)
			FAIL(-status, , "e2label failed with code %i.", -status)
	}
	
	step(d);
	return mount_volume(d);
}

static int mount_volume(Data *d)
{
	ensure_argument(d, &d->dest, "dest");
	
	// Let udisks do the mounting, since it mounts the drive in a
	// unique spot, unlike simply mounting at /mnt.
	// Could do this with udisks dbus API but lazy.
	
	int rfd;
	int status = RUN(&rfd, "udisksctl", "mount", "-b", d->dest);
	if(status > 0)
		return status;
	status = -status;
	
	char buf[1024];
	int num = read(rfd, buf, sizeof(buf));
	
	bool alreadyMounted = false;
	if(status == 0)
	{
		char *phrase = g_strdup_printf("Mounted %s at", d->dest);
		int phraseLen = strlen(phrase);
		const char *loc = g_strstr_len(buf, num, phrase);
		g_free(phrase);
		if(!loc)
			FAIL(status, , "Unexpected output: %.*s", num, buf);
		loc += phraseLen + 1; // +1 for space
		const char *end = strchr(loc, '.');
		if(!end)
			FAIL(status, , "Unexpected output: %.*s", num, buf);
		d->mountPath = g_strndup(loc, (end-loc));
	}
	else
	{
		static char phrase[] = "already mounted at";
		const char *loc = g_strstr_len(buf, num, phrase);
		if(!loc)
			FAIL(status, , "Mount failed: %.*s", num, buf);
		loc += sizeof(phrase) + 1; // +1 for " `", and the other +1 is the NULL byte
		const char *end = strchr(loc, '\'');
		if(!end)
			FAIL(status, , "Unexpected output: %.*s", num, buf);
		d->mountPath = g_strndup(loc, (end-loc));
		println("%s already mounted", d->dest);
		alreadyMounted = true;
	}
	
	println("Mounted at %s", d->mountPath);
	step(d);

	if(chdir(d->mountPath))
		FAIL(errno, , "Failed to chdir to mount path")

	println("Creating directories");
	
	#define TRY_MKDIR(path, mode) { if(mkdir(path, mode) && errno != EEXIST) FAIL(errno,, "Failed to mkdir %s/" path, d->mountPath) }
	TRY_MKDIR("boot", 0755)
	if(d->refind)
		TRY_MKDIR("boot/efi", 0755)
	TRY_MKDIR("etc", 0755)
	TRY_MKDIR("run", 0755)
	TRY_MKDIR("dev", 0755)
	TRY_MKDIR("var", 0755)
	TRY_MKDIR("var/cache", 0755)
	TRY_MKDIR("var/cache/pacman", 0755)
	TRY_MKDIR("var/cache/pacman/pkg", 0755)
	TRY_MKDIR("var/lib", 0755)
	TRY_MKDIR("var/lib/pacman", 0755)
	TRY_MKDIR("var/log", 0755)
	TRY_MKDIR("tmp", 1777)
	TRY_MKDIR("sys", 0555)
	TRY_MKDIR("proc", 0555)
	#undef TRY_MKDIR
	
	println("Mounting temporary filesystems");

	#define TRY_MOUNT(s, t, fs, f, data) { if(mount(s, t, fs, f, data) && errno != EBUSY) FAIL(errno, , "Failed to mount %s/" t, d->mountPath) }
	TRY_MOUNT("proc", "proc", "proc", MS_NOSUID|MS_NOEXEC|MS_NODEV, "")
	TRY_MOUNT("sys", "sys", "sysfs", MS_NOSUID|MS_NOEXEC|MS_NODEV|MS_RDONLY, "")
	mount("efivarfs", "sys/firmware/efi/efivars", "efivarfs", MS_NOSUID|MS_NOEXEC|MS_NODEV, "");
	TRY_MOUNT("udev", "dev", "devtmpfs", MS_NOSUID, "mode=0755")
	TRY_MOUNT("devpts", "dev/pts", "devpts", MS_NOSUID|MS_NOEXEC, "gid=5,mode=0620")
	TRY_MOUNT("shm", "dev/shm", "tmpfs", MS_NOSUID|MS_NODEV, "mode=1777")
	TRY_MOUNT("run", "run", "tmpfs", MS_NOSUID|MS_NODEV, "mode=0755")
	TRY_MOUNT("tmp", "tmp", "tmpfs", MS_NOSUID|MS_NODEV|MS_STRICTATIME, "mode=1777")
	#undef TRY_MOUNT
	
	// Continue install
	int r = run_pacstrap(d);

	// Cleanup

	// gpg-agent is a piece o' trash and loves to just hang around after
	// running pacman-key, and it keeps the drive from being unmounted.
	// XXX: Probably a better solution than this
	status = RUN(NULL, "killall", "-u", "root", "gpg-agent");

	println("Unmounting temporary filesystems");
	errno = 0;
	if(!chdir(d->mountPath))
	{
		umount("tmp");
		umount("run");
		umount("dev/shm");
		umount("dev/pts");
		umount("dev");
		umount("sys/firmware/efi/efivars");
		umount("sys");
		umount("proc");
	}
	if(errno)
		println("Warning: Failed to unmount some. You may have to do this manually.");

	if(!alreadyMounted)
	{
		println("Unmounting volume");
		// udisksctl not necessary for unmount
		umount2(".", MNT_DETACH); // Lazy unmount
	}
	return r;
}

static bool search_file_for_line(FILE *file, const char *search)
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
			return true;
	}
	return false;
}

static int run_pacstrap(Data *d)
{
	char *cachedir = g_build_path("/", d->mountPath, "var", "cache", "pacman", "pkg", NULL);
	
	// Install base first before user packages. That way we can modify
	// pacman.conf's repository list and download signing keys.
	if(!d->skipPacstrap)
	{
		int status;
		if(d->refind)
		{
			status = RUN(NULL,
				"pacman",
				"-r", d->mountPath,
				"--cachedir", cachedir,
				"--noconfirm",
				"-Sy", "base", "refind-efi");
		}
		else
		{
			status = RUN(NULL,
				"pacman",
				"-r", d->mountPath,
				"--cachedir", cachedir,
				"--noconfirm",
				"-Sy", "base");
		}
		
		if(status > 0)
		{
			g_free(cachedir);
			return status;
		}
		else if(status < 0)
			FAIL(-status, g_free(cachedir), "pacman failed with code %i.", -status)
	}
	step(d);

	char *confpath = g_build_path("/", d->mountPath, "etc", "pacman.conf", NULL);
	char *gpgdir = g_build_path("/", d->mountPath, "etc", "pacman.d", "gnupg", NULL);

	// pacman-key init
	{
		char *args[] = {"pacman-key",
			"--config", confpath,
			"--gpgdir", gpgdir,
			"--init",
			NULL};
		
		int status = run(NULL, (const char * const *)args);
		if(status > 0)
			return status;
		else if(status < 0)
			FAIL(-status, , "pacman-key --init failed with code %i.", -status)
		
		args[5] = "--populate";
		
		status = run(NULL, (const char * const *)args);
		if(status > 0)
			return status;
		else if(status < 0)
			FAIL(-status, , "pacman-key --populate failed with code %i.", -status)
	}

	if(d->repos)
	{
		println("Adding repos to %s", confpath);
		FILE *conf = fopen(confpath, "a+");
		
		for(GList *it=d->repos; it!=NULL; it=it->next)
		{
			Repo *repo = it->data;
			println("Adding repo %s...", repo->name);
			
			// Don't add the repo if it's already there
			char *searchFor = g_strdup_printf("[%s]", repo->name);
			bool found = search_file_for_line(conf, searchFor);
			g_free(searchFor);
			
			if(!found)
			{
				fprintf(conf, "\n[%s]\nSigLevel = %s\nServer = %s\n",
					repo->name,
					repo->siglevel,
					repo->server);
			}
			
			println("Downloading signing keys for %s from pgp.mit.edu...", repo->name);
			
			size_t nkeys = g_strv_length(repo->keys);
			char **args = g_new(char *, nkeys+7);
			args[0] = "pacman-key";
			args[1] = "--keyserver";
			args[2] = "pgp.mit.edu";
			args[3] = "--gpgdir";
			args[4] = gpgdir;
			args[5] = "--recv-keys";
			for(size_t i=0;i<nkeys;++i)
				args[6+i] = repo->keys[i];
			args[nkeys+6] = NULL;
			int status = run(NULL, (const char * const *)args);

			if(status > 0)
			{
				g_free(args);
				g_free(confpath);
				g_free(gpgdir);
				g_free(cachedir);
				return status;
			}

			if(status < 0)
			{
				println("pacman-key --recv-keys on pgp.mit.edu failed with code %i", -status);
				println("Trying pool.sks-keyservers.net");
				args[2] = "pool.sks-keyservers.net";
				status = run(NULL, (const char * const *)args);
				if(status)
				{
					g_free(args);
					g_free(confpath);
					g_free(gpgdir);
					g_free(cachedir);
				}
				
				if(status > 0)
					return status;
				else if(status < 0)
					FAIL(-status, , "pacman-key --recv-keys failed with code %i.", -status)
			}
		}
		
		fclose(conf);
	}
	
	step(d);
	
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
	size_t numPackages = 0;
	for(size_t i=0;split[i]!=NULL;++i)
		if(split[i][0] != '\0') // two spaces between packages create empty splits
			numPackages++;
	
	char **args = g_new(char *, numPackages + 12);
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
	for(size_t i=0,j=11;split[i]!=NULL;++i)
	{
		if(split[i][0] != '\0')
		{
			if(g_strcmp0(split[i], "sudo") == 0)
				d->enableSudoWheel = true;
			args[j++] = split[i];
		}
	}
	args[numPackages+11] = '\0';
		
	int status = run(NULL, (const char * const *)args);
	g_free(args);
	g_strfreev(split);
	g_free(confpath);
	g_free(gpgdir);
	g_free(cachedir);
	
	if(status > 0)
		return status;
	else if(status < 0)
		FAIL(-status, , "pacman failed with code %i.", -status)
	
	step(d);
	return run_genfstab(d);
}

static int run_genfstab(Data *d)
{
	char *fstabpath = g_build_path("/", d->mountPath, "etc/fstab", NULL);
	println("Writing generated fstab to %s", fstabpath);
	FILE *fstab = fopen(fstabpath, "w");
	g_free(fstabpath);
	
	if(!fstab)
		FAIL(1, , "Failed to open fstab for writing")

	// Genfstab doesn't always write what we want (for example,
	// writing nosuid under options when installing to a flash drive)
	// so write it outselves.

	const char *fstype = d->writeExt4 ? "ext4" : d->ofstype;
	if(!fstype) // Should never happen, since the drive has already been mounted
		FAIL(1, fclose(fstab), "Unknown filesystem type")

	fprintf(fstab, "# <file system>\t<mount point>\t<fs type>\t<options>\t<dump>\t<pass>\n\n");
	fprintf(fstab, "PARTUUID=%s\t/\t%s\trw,relatime,data=ordered\t0\t1\n",
		d->partuuid,
		fstype);
	
	fclose(fstab);
	
	step(d);
	return run_chroot(d);
}

static int run_chroot(Data *d)
{
	println("Changing root to %s", d->mountPath);
	if(!exitable_chroot(d->mountPath))
		FAIL(1, , "Chroot failed (must run as root).")
	
	step(d);
	int r = set_passwd(d);
	println("Leaving chroot");
	exitable_chroot(NULL);
	return r;
}

static int chpasswd(Data *d, const char *user, const char *password)
{
	println("Running chpasswd on %s", user);
	
	int fd[2];
	if(pipe(fd) || NONBLOCK(fd[0]) || NONBLOCK(fd[1]))
		FAIL(errno, , "Failed to open pipe")
	
	pid_t ppid = getpid();
	pid_t pid = fork();
	
	if(pid == -1)
	{
		FAIL(errno, , "Failed to fork new process")
	}
	else if(pid == 0)
	{
		close(fd[1]);
		dup2(fd[0], STDIN_FILENO);
		
		// See run()
		if(prctl(PR_SET_PDEATHSIG, SIGHUP))
			abort();
		if(getppid() != ppid)
			abort();
		
		char * const args[] = {"chpasswd", NULL};
		execvp("chpasswd", args);
		abort();
	}

	int userlen = strlen(user);
	int pwdlen = strlen(password);
	
	if(write(fd[1], user, userlen) != userlen 
	|| write(fd[1], ":", 1) != 1
	|| write(fd[1], password, pwdlen) != pwdlen
	|| close(fd[1]))
	{
		FAIL(errno, , "Failed to write to chpasswd")
	}

	// See run()
	int exitstatus = 0;
	while(1)
	{
		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(d->selfpipe[0], &rfds);
		errno = 0;
		select(d->selfpipe[0]+1, &rfds, NULL, NULL, NULL);
		if(errno != 0 && errno != EINTR)
			d->killing = true;
		static char dummy[PIPE_BUF];
		while(read(d->selfpipe[0], dummy, sizeof(dummy)) > 0);
		
		if(!d->killing)
		{
			errno = 0;
			if(waitpid(pid, &exitstatus, WNOHANG) > 0)
				break;
			else if(errno == 0 || errno == EINTR) // 0 for WNOHANG
				continue;
		}
		
		// Something went wrong; stop the child process.
		kill(pid, SIGKILL);
		waitpid(pid, NULL, 0);
		
		if(d->killing)
			FAIL(1, , "Install aborted")
		else
			FAIL(errno, , "Error monitoring process")
	}
	
	int exit = 1;
	if(WIFEXITED(exitstatus))
		exit = WEXITSTATUS(exitstatus);
	if(exit != 0)
		FAIL(exit, , "chpasswd failed with code %i.", exit)
	return 0;
}

static int set_passwd(Data *d)
{
	ensure_argument(d, &d->password, "password");
	if(d->password[0] == '\0')
	{
		println("Skipping set password");
		step(d);
		return set_locale(d);
	}
	
	int status = 0;
	if((status = chpasswd(d, "root", d->password)))
		return status;
	
	step(d);
	return set_locale(d);
}

static int set_locale(Data *d)
{
	ensure_argument(d, &d->locale, "locale");
	
	const char *locale = d->locale;
	if(locale[0] == '\0')
		locale = "en_US.UTF-8";
	char *localeesc = g_regex_escape_string(locale, -1);

	// Remove comments from any lines matching the given locale prefix
	char *pattern = g_strdup_printf("s/^#(%s.*$)/\\1/", localeesc);
	int status = RUN(NULL, "sed", "-i", "-E", pattern, "/etc/locale.gen");
	g_free(pattern);
	if(status > 0)
	{
		g_free(localeesc);
		return status;
	}
	else if(status < 0)
		FAIL(-status, g_free(localeesc), "Edit of /etc/locale.gen failed with code %i.", -status)
	
	// Write first locale match to /etc/locale.conf (for the LANG variable)
	
	pattern = g_strdup_printf("^%s", localeesc);
	g_free(localeesc);
	int fd;
	status = RUN(&fd, "grep", "-m1", "-e", pattern, "/etc/locale.gen");
	g_free(pattern);
	if(status > 0)
		return status;
	else if(status < -1) // exit code of 1 means no lines found, not error
		FAIL(-status, , "grep failed with code %i.", -status)
	
	int lconff = open("/etc/locale.conf", O_WRONLY|O_CREAT);
	if(lconff < 0)
		FAIL(errno, , "Failed to open locale.conf for writing")
	
	write(lconff, "LANG=", 5);
	
	char buf[1024];
	size_t num = 0;
	while((num = read(fd, buf, sizeof(buf))) > 0)
	{
		// Only write until the first space
		const char *space = strchr(buf, ' ');
		if(space != NULL)
			num = (space - buf);

		if(write(lconff, buf, num) != (int)num)
			FAIL(1, close(lconff);, "Failed to write %lu bytes to locale.conf", num)
		
		if(space != NULL)
			break;
	}	
	write(lconff, "\n", 1);
	close(lconff);

	// Run locale-gen
	status = RUN(NULL, "locale-gen");
	if(status > 0)
		return status;
	else if(status < 0)
		FAIL(-status, , "locale-gen failed with code %i.", -status)
	
	step(d);
	return set_zone(d);
}

static int set_zone(Data *d)
{
	ensure_argument(d, &d->zone, "zone");
	const char *zone = "UTC";
	if(d->zone[0] != '\0')
		zone = d->zone;
	
	char *path = g_build_path("/", "/usr/share/zoneinfo/", zone, NULL);
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
			println("/etc/localtime already exists, replacing");
			if(!unlink("/etc/localtime"))
				symlink(path, "/etc/localtime");
		}
	}
	
	if(errno)
		FAIL(errno, g_free(path), "Error symlinking: %i", errno);
	
	step(d);
	g_free(path);
	
	// Set /etc/adjtime
	int status = RUN(NULL, "hwclock", "--systohc");
	if(status > 0)
		return status;
	else if(status < 0)
		FAIL(-status, , "Failed to set system clock with error %i.", -status)
	
	step(d);
	return set_hostname(d);
}

static int set_hostname(Data *d)
{
	ensure_argument(d, &d->hostname, "hostname");
	if(d->hostname[0] == '\0')
	{
		println("Skipping setting hostname");
		step(d);
		return create_user(d);
	}
	
	println("Writing %s to hostname", d->hostname);
	int hostf = open("/etc/hostname", O_WRONLY|O_CREAT);
	if(hostf < 0)
		FAIL(errno, , "Failed to open /etc/hostname for writing")
	int len = strlen(d->hostname);
	write(hostf, d->hostname, len);
	write(hostf, "\n", 1);
	close(hostf);
	
	step(d);
	return create_user(d);
}

static int create_user(Data *d)
{
	ensure_argument(d, &d->username, "username");
	if(d->username[0] == '\0')
	{
		println("Skipping create user");
		d->steps++; // create_user has two steps
		step(d);
		return enable_services(d);
	}
	
	int status = RUN(NULL, "useradd", "-m", "-G", "wheel", d->username);
	
	if(status > 0)
		return status;
	// error code 9 is user already existed. As this installer should be
	// repeatable (in order to easily fix problems and retry), ignore this error.
	else if(status != -9 && status < 0)
		FAIL(-status, , "Failed to create user, error code %i.", -status)
	
	ensure_argument(d, &d->password, "password");
	
	if(d->password[0] == '\0')
	{
		println("Skipping set password on user");
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
		println("Skipping set real name on user");
	}
	else
	{
		int status = RUN(NULL, "chfn", "-f", d->name, d->username);
		
		if(status > 0)
			return status;
		else if(status < 0)
			FAIL(-status, , "Failed to create user, error code %i.", -status)
	}
	
	step(d);
	
	// Enable sudo for user
	if(d->enableSudoWheel)
	{
		println("Enabling sudo for user %s", d->username);
		int status = RUN(NULL, "sed", "-i", "-E", "s/#\\s?(%wheel ALL=\\(ALL\\) ALL)/\\1/", "/etc/sudoers");
		if(status > 0)
			return status;
		else if(status < 0)
			FAIL(-status, , "Edit of /etc/sudoers failed with code %i.", -status)
	}
	
	step(d);
	return enable_services(d);
}

static int enable_services(Data *d)
{
	ensure_argument(d, &d->services, "services");
	if(d->services[0] == '\0')
	{
		println("No services to enable");
		step(d);
		return run_postcmd(d);
	}
	
	char ** split = g_strsplit(d->services, " ", -1);
	size_t numServices = 0;
	for(size_t i=0;split[i]!=NULL;++i)
		if(split[i][0] != '\0') // two spaces between services create empty splits
			numServices++;
	
	char **args = g_new(char *, numServices + 3);
	args[0] = "systemctl";
	args[1] = "enable";
	for(size_t i=0,j=2;split[i]!=NULL;++i)
		if(split[i][0] != '\0')
			args[j++] = split[i];
	args[numServices+2] = '\0';
		
	int status = run(NULL, (const char * const *)args);
	g_free(args);
	g_strfreev(split);
	
	if(status > 0)
		return status;
	else if(status < 0)
		FAIL(-status, , "systemctl enable failed with code %i.", -status)
	
	step(d);
	return run_postcmd(d);
}

static int run_postcmd(Data *d)
{
	if(d->postcmds == NULL)
	{
		println("No postcmds");
		step(d);
		return install_refind(d);
	}
	
	for(GList *it=d->postcmds; it!=NULL; it=it->next)
	{
		int status = RUN(NULL, "/bin/sh", "-c", it->data);
		
		if(status > 0)
			return status;
		else if(status < 0)
			FAIL(-status, , "Postcmd '%s' failed with code %i.", (char *)it->data, -status)
	}
	
	step(d);
	return install_refind(d);
}

static int install_refind(Data *d)
{
	if(!d->refind)
	{
		println("Not installing rEFInd bootmanager");
		step(d);
		return 0;
	}

	int status;
	
	// the run_pacstrap section will automatically install
	// the 'refind-efi' package if d->refind
	if(d->refindDest && d->refindExternal)
	{
		println("Installing rEFInd external EFI standard location");
		status = RUN(NULL, "refind-install", "--yes", "--usedefault", d->refindDest);
	}
	else if(d->refindDest && !d->refindExternal)
	{
		println("Installing rEFInd to internal EFI location");
		
		// To force refind-install to  install at a specific drive and
		// still set efivars and such is to mount the drive at
		// /boot/efi before running, AND make sure it's a vfat partition.
		// If those aren't true, refind-install will try to auto-detect
		// an install location, which is not what the user asked for.
		// This should be good enough
		
		// boot/efi created earlier
		// also in chroot still, so use absolute path
		if(mount(d->refindDest, "/boot/efi", "vfat", MS_SYNCHRONOUS, "") && errno != EBUSY)
			FAIL(errno, , "Failed to mount EFI partition")
		
		status = RUN(NULL, "refind-install", "--yes");
		
		umount("/boot/efi");
	}
	else
	{
		println("Installing rEFInd automatically");
		
		status = RUN(NULL, "refind-install", "--yes");
	}
	
	if(status > 0)
		return status;
	else if(status < 0)
		FAIL(-status, , "refind-install failed with code %i.", -status)
	
	step(d);
	return 0;
}
