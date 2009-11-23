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

#elif defined __NetBSD__

#include <time.h>
#include <sys/param.h>
#include <sys/sysctl.h>

#elif defined __sun && defined __SVR4

#include <sys/loadavg.h>
#include <kstat.h>
#include <err.h>

#endif

#define BSIZE 256

/*
#define add_to_uptime(line, c, i) \
    snprintf(line, sizeof(line), "%s %d%c", line, i, c)
*/

char plugin_name[]        = "weenfo";
char plugin_version[]     = "0.3";
char plugin_description[] = "WeeChat sysinfo plugin.";

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
	sysctlbyname("machdep.cpu_brand", &cpuname, &size, NULL, 0);

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

void
add_to_uptime(char *line, char c, int i)
{
	char tmp[8];

	snprintf(tmp, sizeof(tmp), " %d%c", i, c);
	strncat(line, tmp, sizeof(line) - strlen(tmp));
}

/* ------------------------------------------------------------------ */

int
uptime_info(weenfo *info)
{
	uint64_t time;
	uint32_t week, day, hour, min;

#if defined linux

	struct sysinfo sinfo;
	sysinfo(&sinfo);
	time = (uint64_t)sinfo.uptime;

#elif defined __NetBSD__

	int mid[2] = { CTL_KERN, KERN_BOOTTIME };
	struct timeval boottime;
	size_t size = sizeof(boottime);
	time_t now;

	sysctl(mid, 2, &boottime, &size, NULL, 0);

	time(&now);
	time = now - boottime.tv_sec;

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
	time = now - ksd->value.ui64;

	kstat_close(kc);
#endif

	week = (uint32_t) time / (7 * 24 * 3600);
	day  = (uint32_t)(time / (24 * 3600)) % 7;
	hour = (uint32_t)(time / 3600) % 24;
	min  = (uint32_t)(time / 60) % 60;

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
		 buffersMem = 0,
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
			buffersMem = atoi(pos + 2);
		} else if(!strncmp(buffer, "Cached:", 7)) {
			pos = strchr(buffer, ':');
			cachedMem = atoi(pos + 2);
		}
	}
	fclose(fp);

	usedMem = totalMem - freeMem - buffersMem - cachedMem;

	/*
	struct sysinfo sinfo;
	totalMem = sinfo.totalram;
	freeMem = sinfo.freeram;
	buffersMem = sinfo.bufferram;
	cachedMem = sinfo.?;
	*/
#elif defined __NetBSD

	int mib[] = { CTL_VM, VM_UVMEXP2 };
	const int pagesize = getpagesize();
	struct uvmexp_sysctl uvmexp;
	size_t size = sizeof(uvmexp);

	sysctl(mib, 2, &uvmexp, &size, NULL, 0);

	totalMem = uvmexp.npages * pagesize;
	usedMem  = (uvmexp.npages - uvmexp.free - uvmexp.inactive) * pagesize;

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
	    "Memory Usage: %.2fMB/%.2fMB (%.2f%%)",
	    (float)usedMem / 1024, (float)totalMem / 1024,
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
#elif defined __NetBSD__

	int		 mntsize = 0;
	struct statvfs	*mntbuf;
	char		 disk_line[256];
	int		 i;

	mntsize = getmntinfo(&mntbuf, ST_WAIT);

	for(i = 0; i < mntsize; i++) {
		if (strncmp(mntbuf[i].f_mntfromname, "/dev/", 5))
			continue;

		total += mntbuf[i].f_blocks * mntbuf[i].f_bsize;
		used  += mntbuf[i].f_bfree * mntbuf[i].f_bsize;
	}
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
get_weenfo(char **argv, char *line)
{
	weenfo info;

	if ((argv[2][0] == '\0') || !strcmp(argv[2], "all")) {
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
	} else if (!strcmp(argv[2], "uname") || !strcmp(argv[2], "os")) {
		uname_info(&info);
		strncpy(line, info.uname, 512);
	} else if (!strcmp(argv[2], "cpu")) {
		cpu_info(&info);
		strncpy(line, info.cpu, 512);
	} else if (!strcmp(argv[2], "mem")) {
		mem_info(&info);
		strncpy(line, info.mem, 512);
	} else if (!strcmp(argv[2], "disk")) {
		disk_info(&info);
		strncpy(line, info.disk, 512);
	} else if (!strcmp(argv[2], "uptime")) {
		uptime_info(&info);
		strncpy(line, info.uptime, 512);
	} else if (!strcmp(argv[2], "load")) {
		load_info(&info);
		strncpy(line, info.load, 512);
	} else
		line[0] = '\0';

	return 0;
}

/* ------------------------------------------------------------------ */

int
weenfo_cmd(t_weechat_plugin *plugin, int argc, char **argv,
    char *handler_args, void *handler_pointer)
{
	char line[512];
	line[0] = '\0';

	get_weenfo(argv, line);
	if (!strcmp(argv[1], "sys"))
		plugin->exec_command(plugin, NULL, NULL, line);
	else if (!strcmp(argv[1], "esys"))
		plugin->print(plugin, NULL, NULL, line);

	return PLUGIN_RC_OK;
}

/* ------------------------------------------------------------------ */

int
weechat_plugin_init (t_weechat_plugin *plugin)
{
	plugin->cmd_handler_add (plugin, "sys",
	    plugin_description,
	    "options",
	    "options: all, cpu, mem, uname|os, disk, uptime",
	    "all|cpu|mem|uname|os|disk|uptime|load",
	    &weenfo_cmd,
	    NULL, NULL);

	plugin->cmd_handler_add (plugin, "esys",
	    plugin_description,
	    "options",
	    "options: all, cpu, mem, uname|os, disk, uptime",
	    "all|cpu|mem|uname|os|disk|uptime|load",
	    &weenfo_cmd,
	    NULL, NULL);

	return PLUGIN_RC_OK;
}
