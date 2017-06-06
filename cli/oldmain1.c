#include "install.h"

const guint kArchMaxSteps = 3;
#define ABORT_AND_RETURN(d, fmt...) { gchar *line=g_strdup_printf(fmt); abortinstall(d, line); g_free(line); return; }
#define OUTPUT(d, fmt...) { gchar *line=g_strdup_printf(fmt); output(d, line); g_free(line); }

typedef struct
{
	ArchInstallParameters p;
	guint steps;
	GMount *mount;
	gchar *mountPath;
} ArchData;

typedef void (*RunCommandComplete)(ArchData *d);

G_DEFINE_QUARK(arch-install-error-quark, arch_install_error);

static ArchData * make_arch_data(ArchInstallParameters *p);
static void free_arch_params(ArchData *d);
static void abortinstall(ArchData *d, const gchar *reason);
static void output(ArchData *d, const gchar *line);
static void run_command(ArchData *d, RunCommandComplete cb, const gchar *command);
static void mount_volume(ArchData *d);
static void run_pacstrap(ArchData *d);
static void run_genfstab(ArchData *d);

void install_arch(ArchInstallParameters *parameters)
{
	ArchData *d = make_arch_data(parameters);
	mount_volume(d);
}

static ArchData * make_arch_data(ArchInstallParameters *p)
{
	ArchData *n = g_new(ArchData, 1);
	if(p->destination)
		n->p.destination = g_object_ref(p->destination);
	n->p.hostname = g_strdup(p->hostname);
	n->p.username = g_strdup(p->username);
	n->p.password = g_strdup(p->password);
	n->p.locale = g_strdup(p->locale);
	n->p.zone = g_strdup(p->zone);
	n->p.packages = g_strdup(p->packages);
	n->p.verbose = p->verbose;
	n->p.callback = p->callback;
	n->p.userdata = p->userdata;
	n->p.cancellable = p->cancellable; // Steal reference
	n->steps = 0;
	return n;
}

static void free_arch_data(ArchData *d)
{
	if(d->p.destination)
		g_object_unref(d->p.destination);
	g_free(d->p.hostname);
	g_free(d->p.username);
	g_free(d->p.password);
	g_free(d->p.locale);
	g_free(d->p.zone);
	g_free(d->p.packages);
	if(d->p.cancellable)
	{
		g_cancellable_cancel(d->p.cancellable);
		g_object_unref(d->p.cancellable);
	}
	if(d->mount)
		g_object_unref(d->mount);
	g_free(d->mountPath);
	g_free(d);
}

static void abortinstall(ArchData *d, const gchar *reason)
{
	if(d->p.callback)
	{
		gchar *line = g_strdup_printf("Installation failed: %s", reason);
		GError *error = g_error_new_literal(ARCH_INSTALL_ERROR_QUARK, 1, reason);
		d->p.callback(d->p.userdata, line, (gfloat)d->steps / kArchMaxSteps, error);
		g_error_free(error);
		g_free(line);
	}
	free_arch_data(d);
}

static void output(ArchData *d, const gchar *line)
{
	if(d->p.callback)
		d->p.callback(d->p.userdata, line, (gfloat)d->steps / kArchMaxSteps, NULL);
}

typedef struct
{
	GCallback callback;
	gpointer data;
} Closure;

static void run_command_finish(GSubprocess *proc, GAsyncResult *res, Closure *clos);
static void run_command(ArchData *d, RunCommandComplete cb, const gchar *command)
{
	OUTPUT(d, "Running: %s", command);
	
	GError *error = NULL;
	GSubprocess *proc = g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, &error,
		"bash", "-c", command, NULL);
	if(error)
	{
		abortinstall(d, error->message);
		g_error_free(error);
		return;
	}
	
	Closure *closure = g_new(Closure, 1);
	closure->callback = G_CALLBACK(cb);
	closure->data = d;
	
	g_subprocess_wait_async(proc,
		d->p.cancellable,
		(GAsyncReadyCallback)run_command_finish,
		closure);
}

#define run_command_f(d, cb, fmt...) { gchar *line=g_strdup_printf(fmt); run_command(d, cb, line); g_free(line); }

static void run_command_finish(GSubprocess *proc, GAsyncResult *res, Closure *clos)
{
	RunCommandComplete cb = (RunCommandComplete)clos->callback;
	ArchData *d = clos->data;
	g_free(clos);
	
	GError *error = NULL;
	g_subprocess_wait_finish(proc, res, &error);
	if(error)
	{
		abortinstall(d, error->message);
		g_error_free(error);
		return;
	}
	
	gint status = g_subprocess_get_exit_status(proc);
	if(status != 0)
		ABORT_AND_RETURN(d, "Command failed with exit code %i", status);
	
	d->steps++;
	if(cb)
		cb(d);
}



static void mount_volume_finish(GVolume *volume, GAsyncResult *res, ArchData *d);
static void mount_volume(ArchData *d)
{
	GVolume *dest = d->p.destination;
	
	if(!dest || !G_IS_VOLUME(dest))
		ABORT_AND_RETURN(d, "Invalid destination volume");
	
	OUTPUT(d, "Mounting %s", g_volume_get_name(dest));
	
	// Skip mounting if the volume is already mounted
	GMount *mount = NULL;
	if((mount = g_volume_get_mount(dest)))
	{
		d->mount = mount; // g_volume_get_mount is [return full]
		mount_volume_finish(NULL, NULL, d);
		return;
	}
	
	if(!g_volume_can_mount(dest))
		ABORT_AND_RETURN(d, "Unable to mount volume");
	
	g_volume_mount(dest,
		G_MOUNT_MOUNT_NONE,
		g_mount_operation_new(),
		d->p.cancellable,
		(GAsyncReadyCallback)mount_volume_finish,
		d);
}

static void mount_volume_finish(GVolume *volume, GAsyncResult *res, ArchData *d)
{
	gboolean newlyMounted = !d->mount;
	if(newlyMounted)
	{
		GError *error = NULL;
		if(!volume
		|| !res
		|| !g_volume_mount_finish(volume, res, &error)
		|| error
		|| !(d->mount = g_volume_get_mount(volume)))
		{
			if(error)
			{
				abortinstall(d, error->message);
				g_error_free(error);
				return;
			}
			ABORT_AND_RETURN(d, "Error mounting volume");
		}
	}
	
	GFile *root = g_mount_get_root(d->mount);
	d->mountPath = g_file_get_path(root);
	g_object_unref(root);
	
	if(newlyMounted)
	{
		OUTPUT(d, "Drive mounted at %s", d->mountPath);
	}
	else
	{
		OUTPUT(d, "Drive already mounted at %s", d->mountPath);
	}
	
	d->steps++;
	run_pacstrap(d);
}


static void run_pacstrap_finish(ArchData *d);
static void run_pacstrap(ArchData *d)
{
	run_command_f(d, run_pacstrap_finish,
		"pkexec pacstrap %s base %s", d->mountPath, d->p.packages ? d->p.packages : "");
}

static void run_pacstrap_finish(ArchData *d)
{
	run_genfstab(d);
}

static void run_genfstab_finish(ArchData *d);
static void run_genfstab(ArchData *d)
{
	run_command_f(d, run_genfstab_finish,
		"pkexec genfstab %s >> %s/etc/fstab", d->mountPath, d->mountPath);
}

static void run_genfstab_finish(ArchData *d)
{
	g_message("yay");
}
