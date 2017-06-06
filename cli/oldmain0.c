/*
 * Installs Arch Linux with VeltOS packages.
 * Does NOT (yet) deal with paritioning. Therefore, in order to run this script,
 * a valid ext4-formatted partition must exist. This script will mount it.
 * 
 * Effectively runs the following commands:
 *
 * # Mount
 * mkdir /mnt
 * mount [drive] /mnt
 *
 * # Install Arch
 * pacstrap /mnt base base-devel [VeltOS package list]
 * genfstab /mnt >> /mnt/etc/fstab
 * mkinitcpio -p linux
 *
 * # Config
 * passwd -R /mnt [passwd]
 * [uncommenting locales in /mnt/etc/locale.gen]
 * arch-chroot /mnt locale-gen
 * ln -s /mnt/usr/share/zoneinfo/[continent]/[city] /mnt/etc/localtime
 * echo [hostname] > /mnt/etc/hostname
 * 
 * # Install GRUB (optional, EFI only)
 * mkdir /mnt/boot
 * mount [EFI partition] /mnt/boot
 * pacman -S grub
 * grub-install --target=x86_64-efi --efi-directory=/mnt/boot --bootloader-id=arch_grub
 * grub-mkconfig -o /boot/grub/grub.cfg
 * [?] mkdir /boot/EFI/boot
 * [?] cp /boot/EFI/arch_grub/grubx64.efi /boot/EFI/boot/bootx64.efi
 *
 * umount /mnt -R
 * reboot
 */

#include <stdlib.h>
#include <argp.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>

#define KEY_INSTALL_GRUB 1
#define MOUNTPOINT "/mnt"
#define VELTOS_PACKAGES "wicd"

typedef struct argp_option ArgpOption;

typedef struct
{
	int verbose, noConfirm, installGRUB;
	char *destination, *rootPassword, *zoneInfo, *locale, *hostName;
} Arguments;

static ArgpOption options[] =
{
	{"verbose",    'v', 0, 0, "Produce verbose output"},
	{"noconfirm",  'y', 0, 0, "Assume yes to all questions"},
	{"destination",'d', "block device", 0, "Device (normally a disk partition) to install VeltOS to"},
	{"rootpwd",    'p', "passwd", 0, "Root password"},
	{"zoneinfo",   'z', "file", 0, "Zoneinfo file (relative to /usr/share/zoneinfo/)"},
	{"locale",     'l', "locale", 0, "Locale (locale.gen format)"},
	{"hostname",   'h', "name", 0, "Machine hostname"},
	{"installgrub", KEY_INSTALL_GRUB, 0, 0, "[EXPERIMENTAL, EFI ONLY] Installs GRUB"},
	{0}
};

const char *argp_program_version = "vosinstall 0.1";
const char *argp_program_bug_address = "Aidan Shafran <zelbrium@gmail.com>";
static char argp_program_doc[] = "An installer for VeltOS (Arch Linux).";
static int verbose = 0;


static error_t parse_arg(int key, char *arg, struct argp_state *state);


int main(int argc, char **argv)
{
	Arguments arguments = {0};
	static struct argp argp = {options, parse_arg, NULL, argp_program_doc};
	argp_parse(&argp, argc, argv, 0, 0, &arguments);
	
	verbose = arguments.verbose;

	// Validate arguments
	if(!arguments.destination)
	{
		printf("Critical: No install destination specified. Use the -d flag to specify a block device (normally a disk partition) to install VeltOS to.\n");
		return EINVAL;
	}

	// Mount disk
	mkdir(MOUNTPOINT, 0755);
	if(mount(arguments.destination, MOUNTPOINT, "ext4", 0, ""))
	{
		printf("Critical: Failed to mount '%s' to '%s'.\n", arguments.destination, MOUNTPOINT); 
		
		switch(errno)
		{
			case EPERM:
				printf("You do not have permission to mount the device. Please run this program as root.\n");
				break;
			case EINVAL:
				printf("Note that the device must be formatted as ext4.\n");
				break;
		}
		
		return errno;
	}
	
	int ret = 1;
	
	// Install Arch
	ret = WEXITSTATUS(system("pacstrap " MOUNTPOINT " base base-devel " VELTOS_PACKAGES));
	if(ret == 127)
	{
		printf("Critical: pacstrap is not available. Please install the arch-install-scripts package.\n");
		return ENOENT;
	}
	else if(ret != 0)
	{
		printf("Critical: pacstrap failed to install Arch Linux / VeltOS packages.\n");
		return ret;
	}
	
	ret = WEXITSTATUS(system("genfstab " MOUNTPOINT " >> " MOUNTPOINT "/etc/fstab"));
	if(ret)
	{
		printf("Critical: Failed to generate fstab.\n");
		return ret;
	}
	
	// Generate ramdisk
	ret = WEXITSTATUS(system("arch-chroot " MOUNTPOINT " /usr/bin/mkinitcpio -p linux"));
	if(ret)
	{
		printf("Critical: Failed to create an initial ramdisk.\n");
		return ret;
	}
	
	// Set root password
	if(arguments.rootPassword)
	{
		static const int MAX_LEN = 200;
		char passwdCommand[MAX_LEN];
		int count = sprintf(passwdCommand, "echo 'root:%s' | chpasswd -R " MOUNTPOINT, MAX_LEN, arguments.rootPassword);
		if(count >= MAX_LEN)
		{
			printf("Warning: Given password is too long. Not setting any password for the root user.\n");
		}
		else
		{
			ret = WEXITSTATUS(system(passwdCommand));
			if(ret)
				printf("Warning: Failed to set password for root user.\n");
		}
	}
	
	// Set hostname
	if(arguments.hostName)
	{
		static const int MAX_LEN = 200;
		char hostnameCommand[MAX_LEN];
		int count = sprintf(hostnameCommand, "echo '%s' > " MOUNTPOINT "/etc/hostname", MAX_LEN, arguments.hostName);
		ret = WEXITSTATUS(system(hostnameCommand));
		if(ret)
			printf("Warning: Failed to set hostname.\n");
	}
	
	// TODO: Locale and time zone
	
	return 0;
}

static error_t parse_arg(int key, char *arg, struct argp_state *state)
{
	Arguments *arguments = state->input;

	switch (key)
	{
	case 'v': arguments->verbose = 1; break;
	case 'y': arguments->noConfirm = 1; break;
	case KEY_INSTALL_GRUB: arguments->installGRUB = 1; break;
	case 'd': arguments->destination = arg; break;
	case 'p': arguments->rootPassword = arg; break;
	case 'z': arguments->zoneInfo = arg; break;
	case 'l': arguments->locale = arg; break;
	case 'h': arguments->hostName = arg; break;
	default: return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

// static void critical(char *msg, int level)
// {
// 	if(!verbose && level <= 0)
// 		return;
// 		
// 	printf("Critical: %s", msg);
// }
// 
// static char * cat_paths(const char *a, const char *b)
// {
// 	size_t aLen = strlen(a);
// 	size_t bLen = strlen(b);
// 	
// 	char *final = (char *)malloc(aLen + bLen + 2); // +1 for / and +1 for NULL
// 	strncpy(final, a, aLen);
// 	final[aLen] = "/";
// 	strncpy(final+aLen+1, b, bLen);
// 	final[aLen+bLen+1] = '\0';
// 	
// 	return final;
// }

// static int mountDisk(const char *disk, const char *mountpoint)
// {
// 	mkdir(mountpoint, 0755); // Creates mountpoint directory if it doesn't exist.
// 	
// 	// Validate mountpoint 
// 	struct stat mp, mpParent;
// 	char *mountpointParent = cat_paths(mountpoint, "..");
// 	if(stat(mountpoint, &statbuf) || stat(mountpointParent, $mpParent))
// 	{
// 		critical("Failed to stat given mountpoint.");
// 		free(mountpointParent);
// 		return 1;
// 	}
// 	free(mountpointParent);
// 	
// 	if((mp.st_mode & S_IFDIR) != S_IFDIR)
// 	{
// 		critical("Given mountpoint is not a directory.");
// 		return 1;
// 	}
// 	else if(mp.st_dev != mpParent.st_dev)
// 	{
// 		critical("Given mountpoint is already mounted.");
// 		return 1;
// 	}
// 	else if(mp.st_ino == mpParent.st_ino)
// 	{
// 		critical("Given mountpoint is root.")
// 		return 1;
// 	} 
// 	
// 	// Mount
// 	
// 	// mount(disk, )
// }