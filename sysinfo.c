/*
 * Written by Kalman Ham, February 26, 2008.
 * Public domain.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/utsname.h>
#include <sys/statvfs.h>
#include <stdint.h>

#include "weechat-plugin.h"


#if defined linux

#include <sys/sysinfo.h>

#elif defined __NetBSD__ || defined __FreeBSD__

#include <time.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/ucred.h>
#include <sys/mount.h>

#elif defined __sun && defined __SVR4

#include <sys/loadavg.h>
#include <kstat.h>
#include <err.h>

#endif

#define BSIZE 256

#define add_to_uptime(line, c, i) \
    snprintf(line, sizeof(line), "%s %d%c", line, i, c)

WEECHAT_PLUGIN_NAME("sysinfo");
WEECHAT_PLUGIN_DESCRIPTION("WeeChat sysinfo plugin.");
WEECHAT_PLUGIN_AUTHOR("Kambus <kambus@gmail.com>");
WEECHAT_PLUGIN_VERSION("0.3");
WEECHAT_PLUGIN_LICENSE("BSD");

struct t_weechat_plugin *weechat_plugin = NULL;

typedef struct {
	char cpu[256];
	char uname[256];
	char uptime[32];
	char load[32];
	char mem[64];
	char disk[64];
} weenfo;

/* ------------------------------------------------------------------ */

int
cpu_info(weenfo *info)
{
	char	cpu[BSIZE];
	float	mhz = 0;

#if defined linux

	FILE	*fp;
	char	 line[BSIZE];
	char	*pos;

	if ((fp = fopen("/proc/cpuinfo", "r")) == NULL)
		return 1;

	while (fgets(line, BSIZE, fp) != NULL) {
		if (strstr(line, "model name")) {
			pos = strchr(line, ':');
			strncpy(cpu, pos + 2, sizeof(cpu));
		} else if (strstr(line, "cpu MHz")) {
			pos = strchr(line, ':');
			mhz = atof(pos + 2);
		}
	}
	fclose(fp);

	cpu[strlen(cpu) - 1] = '\0';

#elif defined __NetBSD__

	size_t size = sizeof(cpu);
	sysctlbyname("machdep.cpu_brand", &cpu, &size, NULL, 0);

#elif defined __FreeBSD__

	int mid[2] = { CTL_HW, HW_MODEL };
	int fmhz;
	size_t size = sizeof(cpu);

	sysctl(mid, 2, &cpu, &size, NULL, 0);
	size = sizeof(fmhz);
	sysctlbyname("hw.clockrate", &fmhz, &size, NULL, 0);
	mhz = (float)fmhz;

#elif defined __sun && defined __SVR4

	kstat_ctl_t	*kc;
	kstat_t		*ksp;
	kstat_named_t	*ksd;

	kc = kstat_open();

	ksp = kstat_lookup(kc, "cpu_info", 0, "cpu_info0");
	if (ksp == NULL) err(1, "cpu_info0");

	ksd = (kstat_named_t *)kstat_data_lookup(ksp, "brand");
	if (ksd == NULL) err(1, "cpu_info:brand");
	strncpy(cpu, ksd->value.str, BSIZE);

	ksd = (kstat_named_t *)kstat_data_lookup(ksp, "current_clock_Hz");
	if (ksd == NULL) err(1, "cpu_info:current_clock_Hz");
	mhz = (float)ksd->value.ui64 / 1000;

	kstat_close(kc);
#endif

	snprintf(info->cpu, sizeof(info->cpu), "CPU: %s (%.2f GHz)",
	    cpu, mhz / 1000);

	return 0;
}

/* ------------------------------------------------------------------ */

int
uname_info(weenfo *info)
{
	struct utsname n;
	uname(&n);

	snprintf(info->uname, sizeof(info->uname),
	    "OS: %s %s/%s", n.sysname, n.release, n.machine);

	return 0;
}

/* ------------------------------------------------------------------ */

int
uptime_info(weenfo *info)
{
	time_t btime;
	uint32_t week, day, hour, min;

#if defined linux

	struct sysinfo sinfo;
	sysinfo(&sinfo);
	btime = (uint64_t)sinfo.uptime;

#elif defined __NetBSD__ || defined __FreeBSD__

	int mid[2] = { CTL_KERN, KERN_BOOTTIME };
	struct timeval boottime;
	size_t size = sizeof(boottime);
	time_t now;

	sysctl(mid, 2, &boottime, &size, NULL, 0);

	time(&now);
	btime = now - boottime.tv_sec;

#elif defined __sun && defined __SVR4

	kstat_ctl_t	*kc;
	kstat_t		*ksp;
	kstat_named_t	*ksd;
	time_t		 now;

	kc = kstat_open();

	ksp = kstat_lookup(kc, "unix", 0, "system_misc");
	if (ksp == NULL) err(1, "system_misc");
	ksd = (kstat_named_t *)kstat_data_lookup(ksp, "boot_time");
	if (ksp == NULL) err(1, "boot_time");

	time(&now);
	btime = now - ksd->value.ui64;

	kstat_close(kc);
#endif

	week = (uint32_t) btime / (7 * 24 * 3600);
	day  = (uint32_t)(btime / (24 * 3600)) % 7;
	hour = (uint32_t)(btime / 3600) % 24;
	min  = (uint32_t)(btime / 60) % 60;

	strncpy(info->uptime, "Uptime:", sizeof(info->uptime));

	if (week) add_to_uptime(info->uptime, 'w', week);
	if (day)  add_to_uptime(info->uptime, 'd', day);
	if (hour) add_to_uptime(info->uptime, 'h', hour);
	if (min)  add_to_uptime(info->uptime, 'm', min);

	return 0;
}

/* ------------------------------------------------------------------ */

int
load_info(weenfo *info)
{
	double lavg[3];

	getloadavg(lavg, sizeof(lavg) / sizeof(lavg[0]));
	snprintf(info->load, sizeof(info->load),
	    "Load Average: %.2f", lavg[0]);

	return 0;
}

/* ------------------------------------------------------------------ */

int
mem_info(weenfo *info)
{
	uint32_t totalMem   = 0,
		 freeMem    = 0,
		 usedMem    = 0,
		 bufMem	    = 0,
		 cachedMem  = 0;

#if defined linux
	FILE	*fp;
	char	 buffer[BSIZE];

	char	*pos = NULL;

	if ((fp = fopen("/proc/meminfo", "r")) == NULL)
		return 1;

	while (fgets(buffer, BSIZE, fp) != NULL) {
		if (!strncmp(buffer, "MemTotal:", 9)) {
			pos = strchr(buffer, ':');
			totalMem = atoi(pos + 2);
		} else if(!strncmp(buffer, "MemFree:", 8)) {
			pos = strchr(buffer, ':');
			freeMem = atoi(pos + 2);
		} else if(!strncmp(buffer, "Buffers:", 8)) {
			pos = strchr(buffer, ':');
			bufMem = atoi(pos + 2);
		} else if(!strncmp(buffer, "Cached:", 7)) {
			pos = strchr(buffer, ':');
			cachedMem = atoi(pos + 2);
		}
	}
	fclose(fp);

	usedMem = totalMem - freeMem - bufMem - cachedMem;

	/*
	struct sysinfo sinfo;
	totalMem = sinfo.totalram;
	freeMem = sinfo.freeram;
	bufMem = sinfo.bufferram;
	cachedMem = sinfo.?;
	*/
#elif defined __NetBSD__

	int mib[] = { CTL_VM, VM_UVMEXP2 };
	const int pagesize = getpagesize();
	struct uvmexp_sysctl uvm;
	size_t size = sizeof(uvm);

	sysctl(mib, 2, &uvm, &size, NULL, 0);

	totalMem = uvm.npages * pagesize >> 10;
	usedMem = (uvm.npages - uvm.free - uvm.inactive) * pagesize >> 10;

#elif defined __FreeBSD__

	const int pagesize = getpagesize();
	size_t size = sizeof(totalMem);

	sysctlbyname("hw.realmem", &totalMem, &size, NULL, 0);
	size = sizeof(cachedMem);
	sysctlbyname("vm.stats.vm.v_cache_count", &cachedMem, &size, NULL, 0);
	size = sizeof(freeMem);
	sysctlbyname("vm.stats.vm.v_free_count", &freeMem, &size, NULL, 0);
	size = sizeof(bufMem);
	sysctlbyname("vm.stats.vm.v_inactive_count", &bufMem, &size, NULL, 0);

	usedMem = totalMem - (freeMem + bufMem + cachedMem) * pagesize;
	/* We need them in KB... */
	totalMem >>= 10;
	usedMem >>= 10;

#elif defined __sun && __SVR4

	kstat_ctl_t	*kc;
	kstat_t		*ksp;
	kstat_named_t	*ksd;

	long	pagesize;

	kc = kstat_open();

	pagesize = sysconf(_SC_PAGE_SIZE);

	ksp = kstat_lookup(kc, "unix", 0, "system_pages");
	if (ksp == NULL) err(1, "system_pages");

	ksd = (kstat_named_t *)kstat_data_lookup(ksp, "physmem");
	if (ksd == NULL) err(1, "physmem");

	totalMem = ksd->value.ui64 * pagesize;

	ksd = (kstat_named_t *)kstat_data_lookup(ksp, "availrmem");
	if (ksd == NULL) err(1, "availrmem");

	usedMem = ksd->value.ui64 * pagesize;

	kstat_close(kc);
#endif

	snprintf(info->mem, sizeof(info->mem),
	    "Memory Usage: %.2fMB/%dMB (%.2f%%)",
	    (float)usedMem / 1024, totalMem >> 10,
	    (float)usedMem / totalMem * 100);

	return 0;
}

/* ------------------------------------------------------------------ */

int
disk_info(weenfo *info)
{
	uint64_t total = 0,
		 used  = 0;
#if defined linux
	struct statvfs	buf;

	char	 buffer[BSIZE];
	char	 path[BSIZE];
	char	*pos = NULL;

	FILE	*mtab;

	if ((mtab = fopen("/etc/mtab", "r")) == NULL)
		return 1;

	while (fgets(buffer, BSIZE, mtab) != NULL) {
		pos = strchr(buffer, ' ');
		sscanf(pos + 1, "%s ", path);

		statvfs(path, &buf);
		total +=  buf.f_blocks * buf.f_bsize;
		used  += (buf.f_blocks - buf.f_bfree) * buf.f_bsize;
	}
	fclose(mtab);

#elif defined __NetBSD__ || defined __FreeBSD__

	int		 mntsize = 0;
	char		 disk_line[256];
	int		 i;

#if defined __NetBSD__
	struct statvfs  *mntbuf;
	mntsize = getmntinfo(&mntbuf, ST_WAIT);
#else
	struct statfs   *mntbuf;
	mntsize = getmntinfo(&mntbuf, MNT_WAIT);
#endif // netbsd

	for(i = 0; i < mntsize; i++) {
		if (strncmp(mntbuf[i].f_mntfromname, "/dev/", 5))
			continue;

		total += mntbuf[i].f_blocks * mntbuf[i].f_bsize;
		used  += mntbuf[i].f_bfree * mntbuf[i].f_bsize;
	}
	used = total - used;
#endif

	snprintf(info->disk, sizeof(info->disk),
	    "Disk Usage: %.2fGB/%.2fGB",
	    (float)used / (1 << 30),
	    (float)total / (1 << 30));

	return 0;
}

/* ------------------------------------------------------------------ */

void
add_to_line(char *line, char *info)
{
	if(line[0] == '\0')
		strncpy(line, info, 512);
	else {
		strncat(line, " - ", 512 - 3);
		strncat(line, info, 512 - strlen(info));
	}
}

/* ------------------------------------------------------------------ */

int
get_weenfo(char *line, char **argv, int argc)
{
	weenfo info;

	if ((argc < 2) || !strcmp(argv[1], "all")) {
		cpu_info(&info);
		uname_info(&info);
		uptime_info(&info);
		load_info(&info);
		mem_info(&info);
		disk_info(&info);

		add_to_line(line, info.uname);
		add_to_line(line, info.cpu);
		add_to_line(line, info.uptime);
		add_to_line(line, info.load);
		add_to_line(line, info.mem);
		add_to_line(line, info.disk);
	} else if (!strcmp(argv[1], "uname") || !strcmp(argv[1], "os")) {
		uname_info(&info);
		strncpy(line, info.uname, 512);
	} else if (!strcmp(argv[1], "cpu")) {
		cpu_info(&info);
		strncpy(line, info.cpu, 512);
	} else if (!strcmp(argv[1], "mem")) {
		mem_info(&info);
		strncpy(line, info.mem, 512);
	} else if (!strcmp(argv[1], "disk")) {
		disk_info(&info);
		strncpy(line, info.disk, 512);
	} else if (!strcmp(argv[1], "uptime")) {
		uptime_info(&info);
		strncpy(line, info.uptime, 512);
	} else if (!strcmp(argv[1], "load")) {
		load_info(&info);
		strncpy(line, info.load, 512);
	} else
		line[0] = '\0';

	return 0;
}

/* ------------------------------------------------------------------ */

int
weenfo_cmd(void *data, struct t_gui_buffer *buffer, int argc,
    char **argv, char **argv_eol)
{
	char line[512];
	line[0] = '\0';

	get_weenfo(line, argv, argc);
	if (!strcmp(argv[0], "/sys"))
		weechat_command(buffer, line);
	else if (!strcmp(argv[0], "/esys"))
		weechat_printf (buffer, "%s", line);

	return WEECHAT_RC_OK;
}

/* ------------------------------------------------------------------ */

int
weechat_plugin_init (struct t_weechat_plugin *plugin,
    int argc, char *argv[])
{
	weechat_plugin = plugin;

	weechat_hook_command("sys",
	    "Send system informations",
	    "all | cpu | mem | uname|os | disk | uptime | load",
	    NULL,
	    "all|cpu|mem|uname|os|disk|uptime|load",
	    &weenfo_cmd,
	    NULL);

	weechat_hook_command("esys",
	    "Display system informations",
	    "all | cpu | mem | uname|os | disk | uptime | load",
	    NULL,
	    "all|cpu|mem|uname|os|disk|uptime|load",
	    &weenfo_cmd,
	    NULL);

	return WEECHAT_RC_OK;
}
