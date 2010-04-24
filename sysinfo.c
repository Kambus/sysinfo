/*
 * Copyright (c) 2008-2009, Kalman Ham
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/utsname.h>
#include <sys/statvfs.h>
#include <stdint.h>

#include "weechat-plugin.h"


#ifdef __linux__

#include <sys/sysinfo.h>

#elif defined(__NetBSD__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)

#include <time.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <sys/ucred.h>
#include <sys/mount.h>

#elif defined(__sun) && defined(__SVR4)

#include <sys/loadavg.h>
#include <kstat.h>
#include <err.h>
#include <unistd.h>

#endif

#define BSIZE 256
#define LINESIZE 512

/*
#define add_to_uptime(line, c, i) \
    snprintf(line + strlen(line), sizeof(line) + strlen(line), " %d%c", i, c)
*/

WEECHAT_PLUGIN_NAME("sysinfo")
WEECHAT_PLUGIN_DESCRIPTION("WeeChat sysinfo plugin.")
WEECHAT_PLUGIN_AUTHOR("Kambus <kambus@gmail.com>")
WEECHAT_PLUGIN_VERSION("0.6")
WEECHAT_PLUGIN_LICENSE("BSD")

struct t_weechat_plugin *weechat_plugin = NULL;

typedef struct {
	char	cpu[256];
	char	uname[256];
	char	uptime[32];
	char	load[32];
	char	mem[64];
	char	disk[64];
} weenfo;

struct line_t {
	char	str[LINESIZE];
	int	len;
}; 

/* ------------------------------------------------------------------ */

static int
cpu_info(weenfo *info)
{
	char	cpu[BSIZE];
	float	mhz = 0;

#ifdef __linux__

	FILE	*fp;
	char	 line[BSIZE];
	char	*pos;

	if ((fp = fopen("/proc/cpuinfo", "r")) == NULL)
		return 1;

	while (fgets(line, BSIZE, fp) != NULL) {
		if (strstr(line, "model name")) {
			pos = strchr(line, ':');
			strncpy(cpu, pos + 2, sizeof(cpu));
			cpu[sizeof(cpu) - 1] = '\0';
		} else if (strstr(line, "cpu MHz")) {
			pos = strchr(line, ':');
			mhz = atof(pos + 2);
		}
	}
	fclose(fp);

	/* Cut off the line feed. */
	cpu[strlen(cpu) - 1] = '\0';

#elif defined(__NetBSD__)

	size_t size = sizeof(cpu);
	sysctlbyname("machdep.cpu_brand", &cpu, &size, NULL, 0);

	snprintf(info->cpu, sizeof(info->cpu), "CPU: %s", cpu);
	return 0;

#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)

	int mid[2] = { CTL_HW, HW_MODEL };
	uint64_t bmhz;
	size_t size = sizeof(cpu);

	sysctl(mid, 2, &cpu, &size, NULL, 0);
	size = sizeof(bmhz);

#ifdef __OpenBSD__

	mid[1] = HW_CPUSPEED;
	sysctl(mid, 2, &bmhz, &size, NULL, 0);

#elif defined(__FreeBSD__)

	sysctlbyname("hw.clockrate", &bmhz, &size, NULL, 0);

#elif defined(__DragonFly__)

	sysctlbyname("hw.tsc_frequency", &bmhz, &size, NULL, 0);
	bmhz /= 1000000;

#endif /* openbsd */

	mhz = (float)bmhz;

#elif defined(__sun) && defined(__SVR4)

	kstat_ctl_t	*kc;
	kstat_t		*ksp;
	kstat_named_t	*ksd;

	kc = kstat_open();

	ksp = kstat_lookup(kc, "cpu_info", 0, "cpu_info0");
	if (ksp == NULL) err(1, "cpu_info0");

        if (kstat_read(kc, ksp, NULL) == -1) err(1, "kstat_read\n");
	ksd = (kstat_named_t *)kstat_data_lookup(ksp, "brand");
	if (ksd == NULL) err(1, "cpu_info:brand");
	strncpy(cpu, ksd->value.str.addr.ptr, BSIZE);

	ksd = (kstat_named_t *)kstat_data_lookup(ksp, "current_clock_Hz");
	if (ksd == NULL) err(1, "cpu_info:current_clock_Hz");
	mhz = (float)ksd->value.ui64 / 1000000;

	kstat_close(kc);
#endif

	snprintf(info->cpu, sizeof(info->cpu), "CPU: %s (%.2f GHz)",
	    cpu, mhz / 1000);

	return 0;
}

/* ------------------------------------------------------------------ */

static int
uname_info(weenfo *info)
{
	struct utsname n;
	uname(&n);

	snprintf(info->uname, sizeof(info->uname),
	    "OS: %s %s/%s", n.sysname, n.release, n.machine);

	return 0;
}

/* ------------------------------------------------------------------ */

static void
add_to_uptime(weenfo *info, char c, int i)
{
	char tmp[8];

	snprintf(tmp, sizeof(tmp), " %d%c", i, c);
	strncat(info->uptime, tmp, sizeof(info->uptime) - strlen(tmp));
	info->uptime[sizeof(info->uptime) - 1] = '\0';
}

/* ------------------------------------------------------------------ */

static int
uptime_info(weenfo *info)
{
	time_t btime;
	uint32_t week, day, hour, min;

#ifdef __linux__

	struct sysinfo sinfo;
	sysinfo(&sinfo);
	btime = (time_t)sinfo.uptime;

#elif defined(__NetBSD__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)

	int mid[2] = { CTL_KERN, KERN_BOOTTIME };
	struct timeval boottime;
	size_t size = sizeof(boottime);
	time_t now;

	sysctl(mid, 2, &boottime, &size, NULL, 0);

	time(&now);
	btime = now - boottime.tv_sec;

#elif defined(__sun) && defined(__SVR4)

	kstat_ctl_t	*kc;
	kstat_t		*ksp;
	kstat_named_t	*ksd;
	time_t		 now;

	kc = kstat_open();

	ksp = kstat_lookup(kc, "unix", 0, "system_misc");
	if (ksp == NULL) err(1, "system_misc");
        if (kstat_read(kc, ksp, NULL) == -1) err(1, "kstat_read\n");
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

	if (week) add_to_uptime(info, 'w', week);
	if (day)  add_to_uptime(info, 'd', day);
	if (hour) add_to_uptime(info, 'h', hour);
	if (min)  add_to_uptime(info, 'm', min);

	return 0;
}

/* ------------------------------------------------------------------ */

static int
load_info(weenfo *info)
{
	double lavg[3];

	getloadavg(lavg, sizeof(lavg) / sizeof(lavg[0]));
	snprintf(info->load, sizeof(info->load),
	    "Load Average: %.2f", lavg[0]);

	return 0;
}

/* ------------------------------------------------------------------ */

static int
mem_info(weenfo *info)
{
	uint32_t totalMem   = 0;
	uint32_t freeMem    = 0;
	uint32_t usedMem    = 0;
	uint32_t bufMem	    = 0;
	uint32_t cachedMem  = 0;

#ifdef __linux__
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

#elif defined(__NetBSD__) || defined(__OpenBSD__)

#ifdef __NetBSD__
	int mib[] = { CTL_VM, VM_UVMEXP2 };
	struct uvmexp_sysctl uvm;
#else
	int mib[] = { CTL_VM, VM_UVMEXP };
	struct uvmexp uvm;
#endif
	const int pagesize = getpagesize();
	size_t size = sizeof(uvm);

	sysctl(mib, 2, &uvm, &size, NULL, 0);

	totalMem = uvm.npages * pagesize >> 10;
	usedMem = (uvm.npages - uvm.free - uvm.inactive) * pagesize >> 10;

#elif defined(__FreeBSD__) || defined(__DragonFly__)

	const int pagesize = getpagesize();
	size_t size = sizeof(totalMem);

#ifdef __DragonFly__
	sysctlbyname("hw.physmem", &totalMem, &size, NULL, 0);
#else
	sysctlbyname("hw.realmem", &totalMem, &size, NULL, 0);
#endif
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

#elif defined(__sun) && defined(__SVR4)

	kstat_ctl_t	*kc;
	kstat_t		*ksp;
	kstat_named_t	*ksd;

	long	pagesize;

	kc = kstat_open();

	pagesize = sysconf(_SC_PAGE_SIZE);

	ksp = kstat_lookup(kc, "unix", 0, "system_pages");
	if (ksp == NULL) err(1, "system_pages");

        if (kstat_read(kc, ksp, NULL) == -1) err(1, "kstat_read\n");
	ksd = (kstat_named_t *)kstat_data_lookup(ksp, "physmem");
	if (ksd == NULL) err(1, "physmem");

	totalMem = ksd->value.ui64 * pagesize >> 10;

	ksd = (kstat_named_t *)kstat_data_lookup(ksp, "availrmem");
	if (ksd == NULL) err(1, "availrmem");

	usedMem = ksd->value.ui64 * pagesize >> 10;

	kstat_close(kc);
#endif

	snprintf(info->mem, sizeof(info->mem),
	    "Memory Usage: %.2fMB/%dMB (%.2f%%)",
	    (float)usedMem / 1024, totalMem >> 10,
	    (float)usedMem / totalMem * 100);

	return 0;
}

/* ------------------------------------------------------------------ */

static int
disk_info(weenfo *info)
{
	uint64_t total = 0,
		 used  = 0;
#ifdef __linux__
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

#elif defined(__NetBSD__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)

	int		 mntsize = 0;
	int		 i;

#ifdef __NetBSD__
	struct statvfs	*mntbuf;
	mntsize = getmntinfo(&mntbuf, ST_WAIT);
#else
	struct statfs	*mntbuf;
	mntsize = getmntinfo(&mntbuf, MNT_WAIT);
#endif /* netbsd */

	for(i = 0; i < mntsize; i++) {
		if (strncmp(mntbuf[i].f_mntfromname, "/dev/", 5) &&
		    strncmp(mntbuf[i].f_mntfromname, "ROOT", 4))
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

static void
add_to_line(struct line_t *line, char *p)
{       
	int	 i;
	char	*lp = line->str + line->len;

	if (line->len && 3 < LINESIZE - line->len) {
		*lp++ = ' '; 
		*lp++ = '-';
		*lp++ = ' ';
		line->len += 3;
	}

	for (i = 0; *p && i < LINESIZE - line->len; i++) {
		*lp++ = *p++;
	}
	line->len += i;
}

/* ------------------------------------------------------------------ */

static int
get_weenfo(struct line_t *line, char **argv, int argc)
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
		add_to_line(line, info.uname);
	} else if (!strcmp(argv[1], "cpu")) {
		cpu_info(&info);
		add_to_line(line, info.cpu);
	} else if (!strcmp(argv[1], "mem")) {
		mem_info(&info);
		add_to_line(line, info.mem);
	} else if (!strcmp(argv[1], "disk")) {
		disk_info(&info);
		add_to_line(line, info.disk);
	} else if (!strcmp(argv[1], "uptime")) {
		uptime_info(&info);
		add_to_line(line, info.uptime);
	} else if (!strcmp(argv[1], "load")) {
		load_info(&info);
		add_to_line(line, info.load);
	}

	return 0;
}

/* ------------------------------------------------------------------ */

static int
weenfo_cmd(void *data, struct t_gui_buffer *buffer, int argc,
    char **argv, char **argv_eol)
{
	struct line_t line = {"\0", 0};

	get_weenfo(&line, argv, argc);
	if (!strcmp(argv[0], "/sys"))
		weechat_command(buffer, line.str);
	else if (!strcmp(argv[0], "/esys"))
		weechat_printf (buffer, "%s", line.str);

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
