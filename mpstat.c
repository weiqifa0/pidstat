/*
 * mpstat: per-processor statistics
 * (C) 2000-2016 by Sebastien GODARD (sysstat <at> orange.fr)
 *
 ***************************************************************************
 * This program is free software; you can redistribute it and/or modify it *
 * under the terms of the GNU General Public License as published  by  the *
 * Free Software Foundation; either version 2 of the License, or (at  your *
 * option) any later version.                                              *
 *                                                                         *
 * This program is distributed in the hope that it  will  be  useful,  but *
 * WITHOUT ANY WARRANTY; without the implied warranty  of  MERCHANTABILITY *
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License *
 * for more details.                                                       *
 *                                                                         *
 * You should have received a copy of the GNU General Public License along *
 * with this program; if not, write to the Free Software Foundation, Inc., *
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335 USA              *
 ***************************************************************************
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/utsname.h>

#include "version.h"
#include "mpstat.h"
#include "common.h"
#include "rd_stats.h"
#include "count.h"

#include <locale.h>	/* For setlocale() */
#ifdef USE_NLS
#include <libintl.h>
#define _(string) gettext(string)
#else
#define _(string) (string)
#endif

#define SCCSID "@(#)sysstat-" VERSION ": "  __FILE__ " compiled " __DATE__ " " __TIME__
char *sccsid(void) { return (SCCSID); }

unsigned long long uptime[3] = {0, 0, 0};
unsigned long long uptime0[3] = {0, 0, 0};

/* NOTE: Use array of _char_ for bitmaps to avoid endianness problems...*/
unsigned char *cpu_bitmap;	/* Bit 0: Global; Bit 1: 1st proc; etc. */
unsigned char *node_bitmap;	/* Bit 0: Global; Bit 1: 1st NUMA node; etc. */

/* Structures used to save CPU and NUMA nodes CPU stats */
struct stats_cpu *st_cpu[3];
struct stats_cpu *st_node[3];

/*
 * Structure used to save total number of interrupts received
 * among all CPU and for each CPU.
 */
struct stats_irq *st_irq[3];

/*
 * Structures used to save, for each interrupt, the number
 * received by each CPU.
 */
struct stats_irqcpu *st_irqcpu[3];
struct stats_irqcpu *st_softirqcpu[3];

/*
 * Number of CPU per node, e.g.:
 * cpu_per_node[0]: nr of CPU for node 0..
 */
int *cpu_per_node;

/*
 * Node number the CPU belongs to, e.g.:
 * cpu2node[0]: node nr for CPU 0
 */
int *cpu2node;

struct tm mp_tstamp[3];

/* Activity flag */
unsigned int actflags = 0;

unsigned int flags = 0;

/* Interval and count parameters */
long interval = -1, count = 0;

/*
 * Nb of processors on the machine.
 * A value of 2 means there are 2 processors (0 and 1).
 */
int cpu_nr = 0;

/*
 * Highest NUMA node number found on the machine.
 * A value of 0 means node 0 (one node).
 * A value of -1 means no nodes found.
 */
int node_nr = -1;

/* Nb of interrupts per processor */
int irqcpu_nr = 0;
/* Nb of soft interrupts per processor */
int softirqcpu_nr = 0;

struct sigaction alrm_act, int_act;
int sigint_caught = 0;

/*
 ***************************************************************************
 * Print usage and exit
 *
 * IN:
 * @progname	Name of sysstat command
 ***************************************************************************
 */
void usage(char *progname)
{
	fprintf(stderr, _("Usage: %s [ options ] [ <interval> [ <count> ] ]\n"),
		progname);

	fprintf(stderr, _("Options are:\n"
			  "[ -A ] [ -n ] [ -u ] [ -V ] [ -I { SUM | CPU | SCPU | ALL } ]\n"
			  "[ -N { <node_list> | ALL } ] [ -o JSON ] [ -P { <cpu_list> | ON | ALL } ]\n"));
	exit(1);
}

/*
 ***************************************************************************
 * SIGALRM signal handler. No need to reset the handler here.
 *
 * IN:
 * @sig	Signal number.
 ***************************************************************************
 */
void alarm_handler(int sig)
{
	alarm(interval);
}

/*
 ***************************************************************************
 * SIGINT signal handler.
 *
 * IN:
 * @sig	Signal number.
 **************************************************************************
 */
void int_handler(int sig)
{
	sigint_caught = 1;
}

/*
 ***************************************************************************
 * Allocate stats structures and cpu bitmap. Also do it for NUMA nodes
 * (although the machine may not be a NUMA one). Assume that the number of
 * nodes is lower or equal than that of CPU.
 *
 * IN:
 * @nr_cpus	Number of CPUs. This is the real number of available CPUs + 1
 * 		because we also have to allocate a structure for CPU 'all'.
 ***************************************************************************
 */
void salloc_mp_struct(int nr_cpus)
{
	int i;

	for (i = 0; i < 3; i++) {

		if ((st_cpu[i] = (struct stats_cpu *) malloc(STATS_CPU_SIZE * nr_cpus))
		    == NULL) {
			perror("malloc");
			exit(4);
		}
		memset(st_cpu[i], 0, STATS_CPU_SIZE * nr_cpus);

		if ((st_node[i] = (struct stats_cpu *) malloc(STATS_CPU_SIZE * nr_cpus))
		    == NULL) {
			perror("malloc");
			exit(4);
		}
		memset(st_node[i], 0, STATS_CPU_SIZE * nr_cpus);

		if ((st_irq[i] = (struct stats_irq *) malloc(STATS_IRQ_SIZE * nr_cpus))
		    == NULL) {
			perror("malloc");
			exit(4);
		}
		memset(st_irq[i], 0, STATS_IRQ_SIZE * nr_cpus);

		if ((st_irqcpu[i] = (struct stats_irqcpu *) malloc(STATS_IRQCPU_SIZE * nr_cpus * irqcpu_nr))
		    == NULL) {
			perror("malloc");
			exit(4);
		}
		memset(st_irqcpu[i], 0, STATS_IRQCPU_SIZE * nr_cpus * irqcpu_nr);

		if ((st_softirqcpu[i] = (struct stats_irqcpu *) malloc(STATS_IRQCPU_SIZE * nr_cpus * softirqcpu_nr))
		     == NULL) {
			perror("malloc");
			exit(4);
		}
		memset(st_softirqcpu[i], 0, STATS_IRQCPU_SIZE * nr_cpus * softirqcpu_nr);
	}

	if ((cpu_bitmap = (unsigned char *) malloc((nr_cpus >> 3) + 1)) == NULL) {
		perror("malloc");
		exit(4);
	}
	memset(cpu_bitmap, 0, (nr_cpus >> 3) + 1);

	if ((node_bitmap = (unsigned char *) malloc((nr_cpus >> 3) + 1)) == NULL) {
		perror("malloc");
		exit(4);
	}
	memset(node_bitmap, 0, (nr_cpus >> 3) + 1);

	if ((cpu_per_node = (int *) malloc(sizeof(int) * nr_cpus)) == NULL) {
		perror("malloc");
		exit(4);
	}

	if ((cpu2node = (int *) malloc(sizeof(int) * nr_cpus)) == NULL) {
		perror("malloc");
		exit(4);
	}
}

/*
 ***************************************************************************
 * Free structures and bitmap.
 ***************************************************************************
 */
void sfree_mp_struct(void)
{
	int i;

	for (i = 0; i < 3; i++) {
		free(st_cpu[i]);
		free(st_node[i]);
		free(st_irq[i]);
		free(st_irqcpu[i]);
		free(st_softirqcpu[i]);
	}

	free(cpu_bitmap);
	free(node_bitmap);
	free(cpu_per_node);
	free(cpu2node);
}

/*
 ***************************************************************************
 * Get node placement (which node each CPU belongs to, and total number of
 * CPU that each node has).
 *
 * IN:
 * @cpu_nr		Number of CPU on this machine.
 *
 * OUT:
 * @cpu_per_node	Number of CPU per node.
 * @cpu2node		The node the CPU belongs to.
 *
 * RETURNS:
 * Highest node number found (e.g., 0 means node 0).
 * A value of -1 means no nodes have been found.
 ***************************************************************************
 */
int get_node_placement(int cpu_nr, int cpu_per_node[], int cpu2node[])

{
	DIR *dir;
	struct dirent *drd;
	char line[MAX_PF_NAME];
	int cpu, node, node_nr = -1;

	/* Init number of CPU per node */
	memset(cpu_per_node, 0, sizeof(int) * cpu_nr);
	/* CPU belongs to no node by default */
	memset(cpu2node, -1, sizeof(int) * cpu_nr);

	for (cpu = 0; cpu < cpu_nr; cpu++) {
		snprintf(line, MAX_PF_NAME, "%s/cpu%d", SYSFS_DEVCPU, cpu);
		line[MAX_PF_NAME - 1] = '\0';

		/* Open relevant /sys directory */
		if ((dir = opendir(line)) == NULL)
			return -1;

		/* Get current file entry */
		while ((drd = readdir(dir)) != NULL) {

			if (!strncmp(drd->d_name, "node", 4) && isdigit(drd->d_name[4])) {
				node = atoi(drd->d_name + 4);
				if ((node >= cpu_nr) || (node < 0)) {
					/* Assume we cannot have more nodes than CPU */
					closedir(dir);
					return -1;
				}
				cpu_per_node[node]++;
				cpu2node[cpu] = node;
				if (node > node_nr) {
					node_nr = node;
				}
				/* Node placement found for current CPU: Go to next CPU directory */
				break;
			}
		}

		/* Close directory */
		closedir(dir);
	}

	return node_nr;
}

/*
 ***************************************************************************
 * Compute node statistics: Split CPU statistics among nodes.
 *
 * IN:
 * @st_cpu	Array where current CPU stats have been read.
 *
 * OUT:
 * @st_node	Array where CPU stats for each node have been saved.
 ***************************************************************************
 */
void set_node_cpu_stats(struct stats_cpu *st_node, struct stats_cpu *st_cpu)
{
	int cpu;
	struct stats_cpu *st_cpu_i, *st_node_i;

	for (cpu = 0; cpu < cpu_nr; cpu++) {
		/* Don't store stats for node 'all'. They are the same as CPU 'all' */
		st_cpu_i = st_cpu + cpu + 1;
		st_node_i = st_node + cpu2node[cpu] + 1;

		st_node_i->cpu_user += st_cpu_i->cpu_user;
		st_node_i->cpu_nice += st_cpu_i->cpu_nice;
		st_node_i->cpu_sys += st_cpu_i->cpu_sys;
		st_node_i->cpu_idle += st_cpu_i->cpu_idle;
		st_node_i->cpu_iowait += st_cpu_i->cpu_iowait;
		st_node_i->cpu_hardirq += st_cpu_i->cpu_hardirq;
		st_node_i->cpu_softirq += st_cpu_i->cpu_softirq;
		st_node_i->cpu_steal += st_cpu_i->cpu_steal;
		st_node_i->cpu_guest += st_cpu_i->cpu_guest;
		st_node_i->cpu_guest_nice += st_cpu_i->cpu_guest_nice;
	}
}

/*
 ***************************************************************************
 * Display CPU statistics in plain format.
 *
 * IN:
 * @dis		TRUE if a header line must be printed.
 * @g_itv	Interval value in jiffies multiplied by the number of CPU.
 * @prev	Position in array where statistics used	as reference are.
 *		Stats used as reference may be the previous ones read, or
 *		the very first ones when calculating the average.
 * @curr	Position in array where current statistics will be saved.
 * @prev_string	String displayed at the beginning of a header line. This is
 * 		the timestamp of the previous sample, or "Average" when
 * 		displaying average stats.
 * @curr_string	String displayed at the beginning of current sample stats.
 * 		This is the timestamp of the current sample, or "Average"
 * 		when displaying average stats.
 ***************************************************************************
 */
void write_plain_cpu_stats(int dis, unsigned long long g_itv, int prev, int curr,
			   char *prev_string, char *curr_string)
{
	struct stats_cpu *scc, *scp;
	unsigned long long pc_itv;
	int cpu;

	if (dis) {
		printf("\n%-11s  CPU    %%usr   %%nice    %%sys %%iowait    %%irq   "
		       "%%soft  %%steal  %%guest  %%gnice   %%idle\n",
		       prev_string);
	}

	/* Check if we want global stats among all proc */
	if (*cpu_bitmap & 1) {

		printf("%-11s", curr_string);
		cprintf_in(IS_STR, " %s", " all", 0);

		cprintf_pc(10, 7, 2,
			   (st_cpu[curr]->cpu_user - st_cpu[curr]->cpu_guest) <
			   (st_cpu[prev]->cpu_user - st_cpu[prev]->cpu_guest) ?
			   0.0 :
			   ll_sp_value(st_cpu[prev]->cpu_user - st_cpu[prev]->cpu_guest,
				       st_cpu[curr]->cpu_user - st_cpu[curr]->cpu_guest,
				       g_itv),
			   (st_cpu[curr]->cpu_nice - st_cpu[curr]->cpu_guest_nice) <
			   (st_cpu[prev]->cpu_nice - st_cpu[prev]->cpu_guest_nice) ?
			   0.0 :
			   ll_sp_value(st_cpu[prev]->cpu_nice - st_cpu[prev]->cpu_guest_nice,
				       st_cpu[curr]->cpu_nice - st_cpu[curr]->cpu_guest_nice,
				       g_itv),
			   ll_sp_value(st_cpu[prev]->cpu_sys,
				       st_cpu[curr]->cpu_sys,
				       g_itv),
			   ll_sp_value(st_cpu[prev]->cpu_iowait,
				       st_cpu[curr]->cpu_iowait,
				       g_itv),
			   ll_sp_value(st_cpu[prev]->cpu_hardirq,
				       st_cpu[curr]->cpu_hardirq,
				       g_itv),
			   ll_sp_value(st_cpu[prev]->cpu_softirq,
				       st_cpu[curr]->cpu_softirq,
				       g_itv),
			   ll_sp_value(st_cpu[prev]->cpu_steal,
				       st_cpu[curr]->cpu_steal,
				       g_itv),
			   ll_sp_value(st_cpu[prev]->cpu_guest,
				       st_cpu[curr]->cpu_guest,
				       g_itv),
			   ll_sp_value(st_cpu[prev]->cpu_guest_nice,
				       st_cpu[curr]->cpu_guest_nice,
				       g_itv),
			   (st_cpu[curr]->cpu_idle < st_cpu[prev]->cpu_idle) ?
			   0.0 :
			   ll_sp_value(st_cpu[prev]->cpu_idle,
				       st_cpu[curr]->cpu_idle,
				       g_itv));
		printf("\n");
	}

	for (cpu = 1; cpu <= cpu_nr; cpu++) {

		scc = st_cpu[curr] + cpu;
		scp = st_cpu[prev] + cpu;

		/* Check if we want stats about this proc */
		if (!(*(cpu_bitmap + (cpu >> 3)) & (1 << (cpu & 0x07))))
			continue;

		/*
		 * If the CPU is offline then it is omited from /proc/stat
		 * and the sum of all values is zero.
		 * (Remember that guest/guest_nice times are already included in
		 * user/nice modes.)
		 */
		if ((scc->cpu_user    + scc->cpu_nice + scc->cpu_sys   +
		     scc->cpu_iowait  + scc->cpu_idle + scc->cpu_steal +
		     scc->cpu_hardirq + scc->cpu_softirq) == 0) {

			if (!DISPLAY_ONLINE_CPU(flags)) {
				printf("%-11s", curr_string);
				cprintf_in(IS_INT, " %4d", "", cpu - 1);
				cprintf_pc(10, 7, 2,
					   0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
				printf("\n");
			}
			continue;
		}

		printf("%-11s", curr_string);
		cprintf_in(IS_INT, " %4d", "", cpu - 1);

		/* Recalculate itv for current proc */
		pc_itv = get_per_cpu_interval(scc, scp);

		if (!pc_itv) {
			/*
			 * If the CPU is tickless then there is no change in CPU values
			 * but the sum of values is not zero.
			 */
			cprintf_pc(10, 7, 2,
				   0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 100.0);
			printf("\n");
		}

		else {
			cprintf_pc(10, 7, 2,
				   (scc->cpu_user - scc->cpu_guest) < (scp->cpu_user - scp->cpu_guest) ?
				   0.0 :
				   ll_sp_value(scp->cpu_user - scp->cpu_guest,
					       scc->cpu_user - scc->cpu_guest,
					       pc_itv),
				   (scc->cpu_nice - scc->cpu_guest_nice) < (scp->cpu_nice - scp->cpu_guest_nice) ?
				   0.0 :
				   ll_sp_value(scp->cpu_nice - scp->cpu_guest_nice,
					       scc->cpu_nice - scc->cpu_guest_nice,
					       pc_itv),
				   ll_sp_value(scp->cpu_sys,
					       scc->cpu_sys,
					       pc_itv),
				   ll_sp_value(scp->cpu_iowait,
					       scc->cpu_iowait,
					       pc_itv),
				   ll_sp_value(scp->cpu_hardirq,
					       scc->cpu_hardirq,
					       pc_itv),
				   ll_sp_value(scp->cpu_softirq,
					       scc->cpu_softirq,
					       pc_itv),
				   ll_sp_value(scp->cpu_steal,
					       scc->cpu_steal,
					       pc_itv),
				   ll_sp_value(scp->cpu_guest,
					       scc->cpu_guest,
					       pc_itv),
				   ll_sp_value(scp->cpu_guest_nice,
					       scc->cpu_guest_nice,
					       pc_itv),
				   (scc->cpu_idle < scp->cpu_idle) ?
				   0.0 :
				   ll_sp_value(scp->cpu_idle,
					       scc->cpu_idle,
					       pc_itv));
			printf("\n");
		}
	}
}

/*
 ***************************************************************************
 * Display CPU statistics in JSON format.
 *
 * IN:
 * @tab		Number of tabs to print.
 * @g_itv	Interval value in jiffies multiplied by the number of CPU.
 * @prev	Position in array where statistics used	as reference are.
 *		Stats used as reference may be the previous ones read, or
 *		the very first ones when calculating the average.
 * @curr	Position in array where current statistics will be saved.
 ***************************************************************************
 */
void write_json_cpu_stats(int tab, unsigned long long g_itv, int prev, int curr)
{
	struct stats_cpu *scc, *scp;
	unsigned long long pc_itv;
	int cpu, next = FALSE;

	xprintf(tab++, "\"cpu-load\": [");

	/* Check if we want global stats among all proc */
	if (*cpu_bitmap & 1) {

		next = TRUE;
		xprintf0(tab, "{\"cpu\": \"all\", \"usr\": %.2f, \"nice\": %.2f, \"sys\": %.2f, "
			      "\"iowait\": %.2f, \"irq\": %.2f, \"soft\": %.2f, \"steal\": %.2f, "
			      "\"guest\": %.2f, \"gnice\": %.2f, \"idle\": %.2f}",
			 (st_cpu[curr]->cpu_user - st_cpu[curr]->cpu_guest) <
			 (st_cpu[prev]->cpu_user - st_cpu[prev]->cpu_guest) ?
			 0.0 :
			 ll_sp_value(st_cpu[prev]->cpu_user - st_cpu[prev]->cpu_guest,
				     st_cpu[curr]->cpu_user - st_cpu[curr]->cpu_guest,
				     g_itv),
			 (st_cpu[curr]->cpu_nice - st_cpu[curr]->cpu_guest_nice) <
			 (st_cpu[prev]->cpu_nice - st_cpu[prev]->cpu_guest_nice) ?
			 0.0 :
			 ll_sp_value(st_cpu[prev]->cpu_nice - st_cpu[prev]->cpu_guest_nice,
				     st_cpu[curr]->cpu_nice - st_cpu[curr]->cpu_guest_nice,
				     g_itv),
			 ll_sp_value(st_cpu[prev]->cpu_sys,
				     st_cpu[curr]->cpu_sys,
				     g_itv),
			 ll_sp_value(st_cpu[prev]->cpu_iowait,
				     st_cpu[curr]->cpu_iowait,
				     g_itv),
			 ll_sp_value(st_cpu[prev]->cpu_hardirq,
				     st_cpu[curr]->cpu_hardirq,
				     g_itv),
			 ll_sp_value(st_cpu[prev]->cpu_softirq,
				     st_cpu[curr]->cpu_softirq,
				     g_itv),
			 ll_sp_value(st_cpu[prev]->cpu_steal,
				     st_cpu[curr]->cpu_steal,
				     g_itv),
			 ll_sp_value(st_cpu[prev]->cpu_guest,
				     st_cpu[curr]->cpu_guest,
				     g_itv),
			 ll_sp_value(st_cpu[prev]->cpu_guest_nice,
				     st_cpu[curr]->cpu_guest_nice,
				     g_itv),
			 (st_cpu[curr]->cpu_idle < st_cpu[prev]->cpu_idle) ?
			 0.0 :
			 ll_sp_value(st_cpu[prev]->cpu_idle,
				     st_cpu[curr]->cpu_idle,
				     g_itv));
	}

	for (cpu = 1; cpu <= cpu_nr; cpu++) {

		scc = st_cpu[curr] + cpu;
		scp = st_cpu[prev] + cpu;

		/* Check if we want stats about this proc */
		if (!(*(cpu_bitmap + (cpu >> 3)) & (1 << (cpu & 0x07))))
			continue;

		if (next) {
			printf(",\n");
		}
		next = TRUE;

		/*
		 * If the CPU is offline then it is omited from /proc/stat
		 * and the sum of all values is zero.
		 * (Remember that guest/guest_nice times are already included in
		 * user/nice modes.)
		 */
		if ((scc->cpu_user    + scc->cpu_nice + scc->cpu_sys   +
		     scc->cpu_iowait  + scc->cpu_idle + scc->cpu_steal +
		     scc->cpu_hardirq + scc->cpu_softirq) == 0) {

			if (!DISPLAY_ONLINE_CPU(flags)) {
				xprintf0(tab, "{\"cpu\": \"%d\", \"usr\": 0.00, \"nice\": 0.00, "
					      "\"sys\": 0.00, \"iowait\": 0.00, \"irq\": 0.00, "
					      "\"soft\": 0.00, \"steal\": 0.00, \"guest\": 0.00, "
					      "\"gnice\": 0.00, \"idle\": 0.00}", cpu - 1);
			}
			continue;
		}

		/* Recalculate itv for current proc */
		pc_itv = get_per_cpu_interval(scc, scp);

		if (!pc_itv) {
			/*
			 * If the CPU is tickless then there is no change in CPU values
			 * but the sum of values is not zero.
			 */
			xprintf0(tab, "{\"cpu\": \"%d\", \"usr\": 0.00, \"nice\": 0.00, "
				      "\"sys\": 0.00, \"iowait\": 0.00, \"irq\": 0.00, "
				      "\"soft\": 0.00, \"steal\": 0.00, \"guest\": 0.00, "
				      "\"gnice\": 0.00, \"idle\": 100.00}", cpu - 1);
		}

		else {
			xprintf0(tab, "{\"cpu\": \"%d\", \"usr\": %.2f, \"nice\": %.2f, \"sys\": %.2f, "
				      "\"iowait\": %.2f, \"irq\": %.2f, \"soft\": %.2f, \"steal\": %.2f, "
				      "\"guest\": %.2f, \"gnice\": %.2f, \"idle\": %.2f}", cpu - 1,
				 (scc->cpu_user - scc->cpu_guest) < (scp->cpu_user - scp->cpu_guest) ?
				 0.0 :
				 ll_sp_value(scp->cpu_user - scp->cpu_guest,
					     scc->cpu_user - scc->cpu_guest,
					     pc_itv),
				 (scc->cpu_nice - scc->cpu_guest_nice) < (scp->cpu_nice - scp->cpu_guest_nice) ?
				 0.0 :
				 ll_sp_value(scp->cpu_nice - scp->cpu_guest_nice,
					     scc->cpu_nice - scc->cpu_guest_nice,
					     pc_itv),
				 ll_sp_value(scp->cpu_sys,
					     scc->cpu_sys,
					     pc_itv),
				 ll_sp_value(scp->cpu_iowait,
					     scc->cpu_iowait,
					     pc_itv),
				 ll_sp_value(scp->cpu_hardirq,
					     scc->cpu_hardirq,
					     pc_itv),
				 ll_sp_value(scp->cpu_softirq,
					     scc->cpu_softirq,
					     pc_itv),
				 ll_sp_value(scp->cpu_steal,
					     scc->cpu_steal,
					     pc_itv),
				 ll_sp_value(scp->cpu_guest,
					     scc->cpu_guest,
					     pc_itv),
				 ll_sp_value(scp->cpu_guest_nice,
					     scc->cpu_guest_nice,
					     pc_itv),
				 (scc->cpu_idle < scp->cpu_idle) ?
				 0.0 :
				 ll_sp_value(scp->cpu_idle,
					     scc->cpu_idle,
					     pc_itv));
		}
	}
	printf("\n");
	xprintf0(--tab, "]");
}

/*
 ***************************************************************************
 * Display CPU statistics in plain or JSON format.
 *
 * IN:
 * @dis		TRUE if a header line must be printed.
 * @g_itv	Interval value in jiffies multiplied by the number of CPU.
 * @prev	Position in array where statistics used	as reference are.
 *		Stats used as reference may be the previous ones read, or
 *		the very first ones when calculating the average.
 * @curr	Position in array where current statistics will be saved.
 * @prev_string	String displayed at the beginning of a header line. This is
 * 		the timestamp of the previous sample, or "Average" when
 * 		displaying average stats.
 * @curr_string	String displayed at the beginning of current sample stats.
 * 		This is the timestamp of the current sample, or "Average"
 * 		when displaying average stats.
 * @tab		Number of tabs to print (JSON format only).
 * @next	TRUE is a previous activity has been displayed (JSON format
 * 		only).
 ***************************************************************************
 */
void write_cpu_stats(int dis, unsigned long long g_itv, int prev, int curr,
		     char *prev_string, char *curr_string, int tab, int *next)
{
	if (DISPLAY_JSON_OUTPUT(flags)) {
		if (*next) {
			printf(",\n");
		}
		*next = TRUE;
		write_json_cpu_stats(tab, g_itv, prev, curr);
	}
	else {
		write_plain_cpu_stats(dis, g_itv, prev, curr, prev_string, curr_string);
	}
}

/*
 ***************************************************************************
 * Display CPU statistics for NUMA nodes in plain format.
 *
 * IN:
 * @dis		TRUE if a header line must be printed.
 * @g_itv	Interval value in jiffies multiplied by the number of CPU.
 * @itv		Interval value.
 * @prev	Position in array where statistics used	as reference are.
 *		Stats used as reference may be the previous ones read, or
 *		the very first ones when calculating the average.
 * @curr	Position in array where current statistics will be saved.
 * @prev_string	String displayed at the beginning of a header line. This is
 * 		the timestamp of the previous sample, or "Average" when
 * 		displaying average stats.
 * @curr_string	String displayed at the beginning of current sample stats.
 * 		This is the timestamp of the current sample, or "Average"
 * 		when displaying average stats.
 ***************************************************************************
 */
void write_plain_node_stats(int dis, unsigned long long g_itv, unsigned long long itv,
			    int prev, int curr, char *prev_string, char *curr_string)
{
	struct stats_cpu *snc, *snp;
	int node;

	if (dis) {
		printf("\n%-11s NODE    %%usr   %%nice    %%sys %%iowait    %%irq   "
		       "%%soft  %%steal  %%guest  %%gnice   %%idle\n",
		       prev_string);
	}

	/*
	 * Check if we want global stats among all nodes.
	 * Stats are the same as global CPU stats among all processors.
	 */
	if (*node_bitmap & 1) {

		printf("%-11s", curr_string);
		cprintf_in(IS_STR, " %s", " all", 0);

		cprintf_pc(10, 7, 2,
			   (st_cpu[curr]->cpu_user - st_cpu[curr]->cpu_guest) <
			   (st_cpu[prev]->cpu_user - st_cpu[prev]->cpu_guest) ?
			   0.0 :
			   ll_sp_value(st_cpu[prev]->cpu_user - st_cpu[prev]->cpu_guest,
				       st_cpu[curr]->cpu_user - st_cpu[curr]->cpu_guest,
				       g_itv),
			   (st_cpu[curr]->cpu_nice - st_cpu[curr]->cpu_guest_nice) <
			   (st_cpu[prev]->cpu_nice - st_cpu[prev]->cpu_guest_nice) ?
			   0.0 :
			   ll_sp_value(st_cpu[prev]->cpu_nice - st_cpu[prev]->cpu_guest_nice,
				       st_cpu[curr]->cpu_nice - st_cpu[curr]->cpu_guest_nice,
				       g_itv),
			   ll_sp_value(st_cpu[prev]->cpu_sys,
				       st_cpu[curr]->cpu_sys,
				       g_itv),
			   ll_sp_value(st_cpu[prev]->cpu_iowait,
				       st_cpu[curr]->cpu_iowait,
				       g_itv),
			   ll_sp_value(st_cpu[prev]->cpu_hardirq,
				       st_cpu[curr]->cpu_hardirq,
				       g_itv),
			   ll_sp_value(st_cpu[prev]->cpu_softirq,
				       st_cpu[curr]->cpu_softirq,
				       g_itv),
			   ll_sp_value(st_cpu[prev]->cpu_steal,
				       st_cpu[curr]->cpu_steal,
				       g_itv),
			   ll_sp_value(st_cpu[prev]->cpu_guest,
				       st_cpu[curr]->cpu_guest,
				       g_itv),
			   ll_sp_value(st_cpu[prev]->cpu_guest_nice,
				       st_cpu[curr]->cpu_guest_nice,
				       g_itv),
			   (st_cpu[curr]->cpu_idle < st_cpu[prev]->cpu_idle) ?
			   0.0 :
			   ll_sp_value(st_cpu[prev]->cpu_idle,
				       st_cpu[curr]->cpu_idle,
				       g_itv));
		printf("\n");
	}

	for (node = 0; node <= node_nr; node++) {

		snc = st_node[curr] + node + 1;
		snp = st_node[prev] + node + 1;

		/* Check if we want stats about this node */
		if (!(*(node_bitmap + ((node + 1) >> 3)) & (1 << ((node + 1) & 0x07))))
			continue;

		if (!cpu_per_node[node])
			/* No CPU in this node */
			continue;

		printf("%-11s", curr_string);
		cprintf_in(IS_INT, " %4d", "", node);

		cprintf_pc(10, 7, 2,
			   (snc->cpu_user - snc->cpu_guest) < (snp->cpu_user - snp->cpu_guest) ?
			   0.0 :
			   ll_sp_value(snp->cpu_user - snp->cpu_guest,
				       snc->cpu_user - snc->cpu_guest,
				       itv * cpu_per_node[node]),
			   (snc->cpu_nice - snc->cpu_guest_nice) < (snp->cpu_nice - snp->cpu_guest_nice) ?
			   0.0 :
			   ll_sp_value(snp->cpu_nice - snp->cpu_guest_nice,
				       snc->cpu_nice - snc->cpu_guest_nice,
				       itv * cpu_per_node[node]),
			   ll_sp_value(snp->cpu_sys,
				       snc->cpu_sys,
				       itv * cpu_per_node[node]),
			   ll_sp_value(snp->cpu_iowait,
				       snc->cpu_iowait,
				       itv * cpu_per_node[node]),
			   ll_sp_value(snp->cpu_hardirq,
				       snc->cpu_hardirq,
				       itv * cpu_per_node[node]),
			   ll_sp_value(snp->cpu_softirq,
				       snc->cpu_softirq,
				       itv * cpu_per_node[node]),
			   ll_sp_value(snp->cpu_steal,
				       snc->cpu_steal,
				       itv * cpu_per_node[node]),
			   ll_sp_value(snp->cpu_guest,
				       snc->cpu_guest,
				       itv * cpu_per_node[node]),
			   ll_sp_value(snp->cpu_guest_nice,
				       snc->cpu_guest_nice,
				       itv * cpu_per_node[node]),
			   (snc->cpu_idle < snp->cpu_idle) ?
			   0.0 :
			   ll_sp_value(snp->cpu_idle,
				       snc->cpu_idle,
				       itv * cpu_per_node[node]));
		printf("\n");
	}
}

/*
 ***************************************************************************
 * Display CPU statistics for NUMA nodes in JSON format.
 *
 * IN:
 * @tab		Number of tabs to print.
 * @g_itv	Interval value in jiffies multiplied by the number of CPU.
 * @itv		Interval value.
 * @prev	Position in array where statistics used	as reference are.
 *		Stats used as reference may be the previous ones read, or
 *		the very first ones when calculating the average.
 * @curr	Position in array where current statistics will be saved.
 ***************************************************************************
 */
void write_json_node_stats(int tab, unsigned long long g_itv, unsigned long long itv,
			   int prev, int curr)
{
	struct stats_cpu *snc, *snp;
	int node, next = FALSE;

	xprintf(tab++, "\"node-load\": [");

	/* Check if we want global stats among all nodes */
	if (*node_bitmap & 1) {

		next = TRUE;
		xprintf0(tab, "{\"node\": \"all\", \"usr\": %.2f, \"nice\": %.2f, \"sys\": %.2f, "
			      "\"iowait\": %.2f, \"irq\": %.2f, \"soft\": %.2f, \"steal\": %.2f, "
			      "\"guest\": %.2f, \"gnice\": %.2f, \"idle\": %.2f}",
			 (st_cpu[curr]->cpu_user - st_cpu[curr]->cpu_guest) <
			 (st_cpu[prev]->cpu_user - st_cpu[prev]->cpu_guest) ?
			 0.0 :
			 ll_sp_value(st_cpu[prev]->cpu_user - st_cpu[prev]->cpu_guest,
				     st_cpu[curr]->cpu_user - st_cpu[curr]->cpu_guest,
				     g_itv),
			 (st_cpu[curr]->cpu_nice - st_cpu[curr]->cpu_guest_nice) <
			 (st_cpu[prev]->cpu_nice - st_cpu[prev]->cpu_guest_nice) ?
			 0.0 :
			 ll_sp_value(st_cpu[prev]->cpu_nice - st_cpu[prev]->cpu_guest_nice,
				     st_cpu[curr]->cpu_nice - st_cpu[curr]->cpu_guest_nice,
				     g_itv),
			 ll_sp_value(st_cpu[prev]->cpu_sys,
				     st_cpu[curr]->cpu_sys,
				     g_itv),
			 ll_sp_value(st_cpu[prev]->cpu_iowait,
				     st_cpu[curr]->cpu_iowait,
				     g_itv),
			 ll_sp_value(st_cpu[prev]->cpu_hardirq,
				     st_cpu[curr]->cpu_hardirq,
				     g_itv),
			 ll_sp_value(st_cpu[prev]->cpu_softirq,
				     st_cpu[curr]->cpu_softirq,
				     g_itv),
			 ll_sp_value(st_cpu[prev]->cpu_steal,
				     st_cpu[curr]->cpu_steal,
				     g_itv),
			 ll_sp_value(st_cpu[prev]->cpu_guest,
				     st_cpu[curr]->cpu_guest,
				     g_itv),
			 ll_sp_value(st_cpu[prev]->cpu_guest_nice,
				     st_cpu[curr]->cpu_guest_nice,
				     g_itv),
			 (st_cpu[curr]->cpu_idle < st_cpu[prev]->cpu_idle) ?
			 0.0 :
			 ll_sp_value(st_cpu[prev]->cpu_idle,
				     st_cpu[curr]->cpu_idle,
				     g_itv));
	}

	for (node = 0; node <= node_nr; node++) {

		snc = st_node[curr] + node + 1;
		snp = st_node[prev] + node + 1;

		/* Check if we want stats about this node */
		if (!(*(node_bitmap + ((node + 1) >> 3)) & (1 << ((node + 1) & 0x07))))
			continue;

		if (!cpu_per_node[node])
			/* No CPU in this node */
			continue;

		if (next) {
			printf(",\n");
		}
		next = TRUE;

		xprintf0(tab, "{\"node\": \"%d\", \"usr\": %.2f, \"nice\": %.2f, \"sys\": %.2f, "
			      "\"iowait\": %.2f, \"irq\": %.2f, \"soft\": %.2f, \"steal\": %.2f, "
			      "\"guest\": %.2f, \"gnice\": %.2f, \"idle\": %.2f}", node,
			 (snc->cpu_user - snc->cpu_guest) < (snp->cpu_user - snp->cpu_guest) ?
			 0.0 :
			 ll_sp_value(snp->cpu_user - snp->cpu_guest,
				     snc->cpu_user - snc->cpu_guest,
				     itv * cpu_per_node[node]),
			 (snc->cpu_nice - snc->cpu_guest_nice) < (snp->cpu_nice - snp->cpu_guest_nice) ?
			 0.0 :
			 ll_sp_value(snp->cpu_nice - snp->cpu_guest_nice,
				     snc->cpu_nice - snc->cpu_guest_nice,
				     itv * cpu_per_node[node]),
			 ll_sp_value(snp->cpu_sys,
				     snc->cpu_sys,
				     itv * cpu_per_node[node]),
			 ll_sp_value(snp->cpu_iowait,
				     snc->cpu_iowait,
				     itv * cpu_per_node[node]),
			 ll_sp_value(snp->cpu_hardirq,
				     snc->cpu_hardirq,
				     itv * cpu_per_node[node]),
			 ll_sp_value(snp->cpu_softirq,
				     snc->cpu_softirq,
				     itv * cpu_per_node[node]),
			 ll_sp_value(snp->cpu_steal,
				     snc->cpu_steal,
				     itv * cpu_per_node[node]),
			 ll_sp_value(snp->cpu_guest,
				     snc->cpu_guest,
				     itv * cpu_per_node[node]),
			 ll_sp_value(snp->cpu_guest_nice,
				     snc->cpu_guest_nice,
				     itv * cpu_per_node[node]),
			 (snc->cpu_idle < snp->cpu_idle) ?
			 0.0 :
			 ll_sp_value(snp->cpu_idle,
				     snc->cpu_idle,
				     itv * cpu_per_node[node]));
	}
	printf("\n");
	xprintf0(--tab, "]");
}

/*
 ***************************************************************************
 * Display nodes statistics in plain or JSON format.
 *
 * IN:
 * @dis		TRUE if a header line must be printed.
 * @g_itv	Interval value in jiffies multiplied by the number of CPU.
 * @itv		Interval value.
 * @prev	Position in array where statistics used	as reference are.
 *		Stats used as reference may be the previous ones read, or
 *		the very first ones when calculating the average.
 * @curr	Position in array where current statistics will be saved.
 * @prev_string	String displayed at the beginning of a header line. This is
 * 		the timestamp of the previous sample, or "Average" when
 * 		displaying average stats.
 * @curr_string	String displayed at the beginning of current sample stats.
 * 		This is the timestamp of the current sample, or "Average"
 * 		when displaying average stats.
 * @tab		Number of tabs to print (JSON format only).
 * @next	TRUE is a previous activity has been displayed (JSON format
 * 		only).
 ***************************************************************************
 */
void write_node_stats(int dis, unsigned long long g_itv, unsigned long long itv,
		      int prev, int curr, char *prev_string, char *curr_string,
		      int tab, int *next)
{
	if (DISPLAY_JSON_OUTPUT(flags)) {
		if (*next) {
			printf(",\n");
		}
		*next = TRUE;
		write_json_node_stats(tab, g_itv, itv, prev, curr);
	}
	else {
		write_plain_node_stats(dis, g_itv, itv, prev, curr, prev_string, curr_string);
	}
}

/*
 ***************************************************************************
 * Display total number of interrupts per CPU in plain format.
 *
 * IN:
 * @dis		TRUE if a header line must be printed.
 * @itv		Interval value.
 * @prev	Position in array where statistics used	as reference are.
 *		Stats used as reference may be the previous ones read, or
 *		the very first ones when calculating the average.
 * @curr	Position in array where current statistics will be saved.
 * @prev_string	String displayed at the beginning of a header line. This is
 * 		the timestamp of the previous sample, or "Average" when
 * 		displaying average stats.
 * @curr_string	String displayed at the beginning of current sample stats.
 * 		This is the timestamp of the current sample, or "Average"
 * 		when displaying average stats.
 ***************************************************************************
 */
void write_plain_isumcpu_stats(int dis, unsigned long long itv, int prev, int curr,
			       char *prev_string, char *curr_string)
{
	struct stats_cpu *scc, *scp;
	struct stats_irq *sic, *sip;
	unsigned long long pc_itv;
	int cpu;

	if (dis) {
		printf("\n%-11s  CPU    intr/s\n", prev_string);
		}

	if (*cpu_bitmap & 1) {
		printf("%-11s", curr_string);
		cprintf_in(IS_STR, " %s", " all", 0);
		/* Print total number of interrupts among all cpu */
		cprintf_f(-1, 1, 9, 2,
			  S_VALUE(st_irq[prev]->irq_nr, st_irq[curr]->irq_nr, itv));
		printf("\n");
	}

	for (cpu = 1; cpu <= cpu_nr; cpu++) {

		sic = st_irq[curr] + cpu;
		sip = st_irq[prev] + cpu;

		scc = st_cpu[curr] + cpu;
		scp = st_cpu[prev] + cpu;

		/* Check if we want stats about this CPU */
		if (!(*(cpu_bitmap + (cpu >> 3)) & (1 << (cpu & 0x07))))
			continue;

		if ((scc->cpu_user    + scc->cpu_nice + scc->cpu_sys   +
		     scc->cpu_iowait  + scc->cpu_idle + scc->cpu_steal +
		     scc->cpu_hardirq + scc->cpu_softirq) == 0) {

			/* This is an offline CPU */

			if (!DISPLAY_ONLINE_CPU(flags)) {
				/*
				 * Display offline CPU if requested by the user.
				 * Value displayed is 0.00.
				 */
				printf("%-11s", curr_string);
				cprintf_in(IS_INT, " %4d", "", cpu - 1);
				cprintf_f(-1, 1, 9, 2, 0.0);
				printf("\n");
			}
			continue;
		}

		printf("%-11s", curr_string);
		cprintf_in(IS_INT, " %4d", "", cpu - 1);

		/* Recalculate itv for current proc */
		pc_itv = get_per_cpu_interval(scc, scp);

		if (!pc_itv) {
			/* This is a tickless CPU: Value displayed is 0.00 */
			cprintf_f(-1, 1, 9, 2, 0.0);
			printf("\n");
		}
		else {
			/* Display total number of interrupts for current CPU */
			cprintf_f(-1, 1, 9, 2,
				  S_VALUE(sip->irq_nr, sic->irq_nr, itv));
			printf("\n");
		}
	}
}

/*
 ***************************************************************************
 * Display total number of interrupts per CPU in JSON format.
 *
 * IN:
 * @tab		Number of tabs to print.
 * @itv		Interval value.
 * @prev	Position in array where statistics used	as reference are.
 *		Stats used as reference may be the previous ones read, or
 *		the very first ones when calculating the average.
 * @curr	Position in array where current statistics will be saved.
 ***************************************************************************
 */
void write_json_isumcpu_stats(int tab, unsigned long long itv, int prev, int curr)
{
	struct stats_cpu *scc, *scp;
	struct stats_irq *sic, *sip;
	unsigned long long pc_itv;
	int cpu, next = FALSE;

	xprintf(tab++, "\"sum-interrupts\": [");

	if (*cpu_bitmap & 1) {

		next = TRUE;
		/* Print total number of interrupts among all cpu */
		xprintf0(tab, "{\"cpu\": \"all\", \"intr\": %.2f}",
			 S_VALUE(st_irq[prev]->irq_nr, st_irq[curr]->irq_nr, itv));
	}

	for (cpu = 1; cpu <= cpu_nr; cpu++) {

		sic = st_irq[curr] + cpu;
		sip = st_irq[prev] + cpu;

		scc = st_cpu[curr] + cpu;
		scp = st_cpu[prev] + cpu;

		/* Check if we want stats about this CPU */
		if (!(*(cpu_bitmap + (cpu >> 3)) & (1 << (cpu & 0x07))))
			continue;

		if (next) {
			printf(",\n");
		}
		next = TRUE;

		if ((scc->cpu_user    + scc->cpu_nice + scc->cpu_sys   +
		     scc->cpu_iowait  + scc->cpu_idle + scc->cpu_steal +
		     scc->cpu_hardirq + scc->cpu_softirq) == 0) {

			/* This is an offline CPU */

			if (!DISPLAY_ONLINE_CPU(flags)) {
				/*
				 * Display offline CPU if requested by the user.
				 * Value displayed is 0.00.
				 */
				xprintf0(tab, "{\"cpu\": \"%d\", \"intr\": 0.00}",
					 cpu - 1);
			}
			continue;
		}

		/* Recalculate itv for current proc */
		pc_itv = get_per_cpu_interval(scc, scp);

		if (!pc_itv) {
			/* This is a tickless CPU: Value displayed is 0.00 */
			xprintf0(tab, "{\"cpu\": \"%d\", \"intr\": 0.00}",
				 cpu - 1);
		}
		else {
			/* Display total number of interrupts for current CPU */
			xprintf0(tab, "{\"cpu\": \"%d\", \"intr\": %.2f}",
				 cpu - 1,
				 S_VALUE(sip->irq_nr, sic->irq_nr, itv));
		}
	}
	printf("\n");
	xprintf0(--tab, "]");
}

/*
 ***************************************************************************
 * Display total number of interrupts per CPU in plain or JSON format.
 *
 * IN:
 * @dis		TRUE if a header line must be printed.
 * @itv		Interval value.
 * @prev	Position in array where statistics used	as reference are.
 *		Stats used as reference may be the previous ones read, or
 *		the very first ones when calculating the average.
 * @curr	Position in array where current statistics will be saved.
 * @prev_string	String displayed at the beginning of a header line. This is
 * 		the timestamp of the previous sample, or "Average" when
 * 		displaying average stats.
 * @curr_string	String displayed at the beginning of current sample stats.
 * 		This is the timestamp of the current sample, or "Average"
 * 		when displaying average stats.
 * @tab		Number of tabs to print (JSON format only).
 * @next	TRUE is a previous activity has been displayed (JSON format
 * 		only).
 ***************************************************************************
 */
void write_isumcpu_stats(int dis, unsigned long long itv, int prev, int curr,
		     char *prev_string, char *curr_string, int tab, int *next)
{
	if (DISPLAY_JSON_OUTPUT(flags)) {
		if (*next) {
			printf(",\n");
		}
		*next = TRUE;
		write_json_isumcpu_stats(tab, itv, prev, curr);
	}
	else {
		write_plain_isumcpu_stats(dis, itv, prev, curr, prev_string, curr_string);
	}
}

/*
 ***************************************************************************
 * Display interrupts statistics for each CPU in plain format.
 *
 * IN:
 * @st_ic	Array for per-CPU statistics.
 * @ic_nr	Number of interrupts (hard or soft) per CPU.
 * @dis		TRUE if a header line must be printed.
 * @itv		Interval value.
 * @prev	Position in array where statistics used	as reference are.
 *		Stats used as reference may be the previous ones read, or
 *		the very first ones when calculating the average.
 * @curr	Position in array where current statistics will be saved.
 * @prev_string	String displayed at the beginning of a header line. This is
 * 		the timestamp of the previous sample, or "Average" when
 * 		displaying average stats.
 * @curr_string	String displayed at the beginning of current sample stats.
 * 		This is the timestamp of the current sample, or "Average"
 * 		when displaying average stats.
 ***************************************************************************
 */
void write_plain_irqcpu_stats(struct stats_irqcpu *st_ic[], int ic_nr, int dis,
			      unsigned long long itv, int prev, int curr,
			      char *prev_string, char *curr_string)
{
	struct stats_cpu *scc;
	int j = ic_nr, offset, cpu, colwidth[NR_IRQS];
	struct stats_irqcpu *p, *q, *p0, *q0;

	/*
	 * Check if number of interrupts has changed.
	 * If this is the case, the header line will be printed again.
	 * NB: A zero interval value indicates that we are
	 * displaying statistics since system startup.
	 */
	if (!dis && interval) {
		for (j = 0; j < ic_nr; j++) {
			p0 = st_ic[curr] + j;
			q0 = st_ic[prev] + j;
			if (strcmp(p0->irq_name, q0->irq_name))
				/*
				 * These are two different interrupts: The header must be displayed
				 * (maybe an interrupt has disappeared, or a new one has just been registered).
				 * Note that we compare even empty strings for the case where
				 * a disappearing interrupt would be the last one in the list.
				 */
				break;
		}
	}

	if (dis || (j < ic_nr)) {
		/* Print header */
		printf("\n%-11s  CPU", prev_string);
		for (j = 0; j < ic_nr; j++) {
			p0 = st_ic[curr] + j;
			if (p0->irq_name[0] == '\0')
				/* End of the list of interrupts */
				break;
			printf(" %8s/s", p0->irq_name);
		}
		printf("\n");
	}

	/* Calculate column widths */
	for (j = 0; j < ic_nr; j++) {
		p0 = st_ic[curr] + j;
		/*
		 * Width is IRQ name + 2 for the trailing "/s".
		 * Width is calculated even for "undefined" interrupts (with
		 * an empty irq_name string) to quiet code analysis tools.
		 */
		colwidth[j] = strlen(p0->irq_name) + 2;
		/*
		 * Normal space for printing a number is 11 chars
		 * (space + 10 digits including the period).
		 */
		if (colwidth[j] < 10) {
			colwidth[j] = 10;
		}
	}

	for (cpu = 1; cpu <= cpu_nr; cpu++) {

		scc = st_cpu[curr] + cpu;

		/*
		 * Check if we want stats about this CPU.
		 * CPU must have been explicitly selected using option -P,
		 * else we display every CPU.
		 */
		if (!(*(cpu_bitmap + (cpu >> 3)) & (1 << (cpu & 0x07))) && USE_P_OPTION(flags))
			continue;

		if ((scc->cpu_user    + scc->cpu_nice + scc->cpu_sys   +
		     scc->cpu_iowait  + scc->cpu_idle + scc->cpu_steal +
		     scc->cpu_hardirq + scc->cpu_softirq) == 0) {

			/* Offline CPU found */

			if (DISPLAY_ONLINE_CPU(flags))
				continue;
		}

		printf("%-11s", curr_string);
		cprintf_in(IS_INT, "  %3d", "", cpu - 1);

		for (j = 0; j < ic_nr; j++) {
			p0 = st_ic[curr] + j;	/* irq_name set only for CPU#0 */
			/*
			 * An empty string for irq_name means it is a remaining interrupt
			 * which is no longer used, for example because the
			 * number of interrupts has decreased in /proc/interrupts.
			 */
			if (p0->irq_name[0] == '\0')
				/* End of the list of interrupts */
				break;
			q0 = st_ic[prev] + j;
			offset = j;

			/*
			 * If we want stats for the time since system startup,
			 * we have p0->irq_name != q0->irq_name, since q0 structure
			 * is completely set to zero.
			 */
			if (strcmp(p0->irq_name, q0->irq_name) && interval) {
				/* Check if interrupt exists elsewhere in list */
				for (offset = 0; offset < ic_nr; offset++) {
					q0 = st_ic[prev] + offset;
					if (!strcmp(p0->irq_name, q0->irq_name))
						/* Interrupt found at another position */
						break;
				}
			}

			p = st_ic[curr] + (cpu - 1) * ic_nr + j;

			if (!strcmp(p0->irq_name, q0->irq_name) || !interval) {
				q = st_ic[prev] + (cpu - 1) * ic_nr + offset;
				cprintf_f(-1, 1, colwidth[j], 2,
					  S_VALUE(q->interrupt, p->interrupt, itv));
			}
			else {
				/*
				 * Instead of printing "N/A", assume that previous value
				 * for this new interrupt was zero.
				 */
				cprintf_f(-1, 1, colwidth[j], 2,
					  S_VALUE(0, p->interrupt, itv));
			}
		}
		printf("\n");
	}
}

/*
 ***************************************************************************
 * Display interrupts statistics for each CPU in JSON format.
 *
 * IN:
 * @tab		Number of tabs to print.
 * @st_ic	Array for per-CPU statistics.
 * @ic_nr	Number of interrupts (hard or soft) per CPU.
 * @itv		Interval value.
 * @prev	Position in array where statistics used	as reference are.
 *		Stats used as reference may be the previous ones read, or
 *		the very first ones when calculating the average.
 * @curr	Position in array where current statistics will be saved.
 * @type	Activity (M_D_IRQ_CPU or M_D_SOFTIRQS).
 ***************************************************************************
 */
void write_json_irqcpu_stats(int tab, struct stats_irqcpu *st_ic[], int ic_nr,
			     unsigned long long itv, int prev, int curr, int type)
{
	struct stats_cpu *scc;
	int j = ic_nr, offset, cpu;
	struct stats_irqcpu *p, *q, *p0, *q0;
	int nextcpu = FALSE, nextirq;

	if (type == M_D_IRQ_CPU) {
		xprintf(tab++, "\"individual-interrupts\": [");
	}
	else {
		xprintf(tab++, "\"soft-interrupts\": [");
	}

	for (cpu = 1; cpu <= cpu_nr; cpu++) {

		scc = st_cpu[curr] + cpu;

		/*
		 * Check if we want stats about this CPU.
		 * CPU must have been explicitly selected using option -P,
		 * else we display every CPU.
		 */
		if (!(*(cpu_bitmap + (cpu >> 3)) & (1 << (cpu & 0x07))) && USE_P_OPTION(flags))
			continue;

		if ((scc->cpu_user    + scc->cpu_nice + scc->cpu_sys   +
		     scc->cpu_iowait  + scc->cpu_idle + scc->cpu_steal +
		     scc->cpu_hardirq + scc->cpu_softirq) == 0) {

			/* Offline CPU found */

			if (DISPLAY_ONLINE_CPU(flags))
				continue;
		}

		if (nextcpu) {
			printf(",\n");
		}
		nextcpu = TRUE;
		nextirq = FALSE;
		xprintf(tab++, "{\"cpu\": \"%d\", \"intr\": [", cpu - 1);

		for (j = 0; j < ic_nr; j++) {

			p0 = st_ic[curr] + j;	/* irq_name set only for CPU#0 */
			/*
			 * An empty string for irq_name means it is a remaining interrupt
			 * which is no longer used, for example because the
			 * number of interrupts has decreased in /proc/interrupts.
			 */
			if (p0->irq_name[0] == '\0')
				/* End of the list of interrupts */
				break;
			q0 = st_ic[prev] + j;
			offset = j;

			if (nextirq) {
				printf(",\n");
			}
			nextirq = TRUE;

			/*
			 * If we want stats for the time since system startup,
			 * we have p0->irq_name != q0->irq_name, since q0 structure
			 * is completely set to zero.
			 */
			if (strcmp(p0->irq_name, q0->irq_name) && interval) {
				/* Check if interrupt exists elsewhere in list */
				for (offset = 0; offset < ic_nr; offset++) {
					q0 = st_ic[prev] + offset;
					if (!strcmp(p0->irq_name, q0->irq_name))
						/* Interrupt found at another position */
						break;
				}
			}

			p = st_ic[curr] + (cpu - 1) * ic_nr + j;

			if (!strcmp(p0->irq_name, q0->irq_name) || !interval) {
				q = st_ic[prev] + (cpu - 1) * ic_nr + offset;
				xprintf0(tab, "{\"name\": \"%s\", \"value\": %.2f}",
					 p0->irq_name,
					 S_VALUE(q->interrupt, p->interrupt, itv));
			}
			else {
				/*
				 * Instead of printing "N/A", assume that previous value
				 * for this new interrupt was zero.
				 */
				xprintf0(tab, "{\"name\": \"%s\", \"value\": %.2f}",
					 p0->irq_name,
					 S_VALUE(0, p->interrupt, itv));
			}
		}
		printf("\n");
		xprintf0(--tab, "] }");
	}
	printf("\n");
	xprintf0(--tab, "]");
}

/*
 ***************************************************************************
 * Display interrupts statistics for each CPU in plain or JSON format.
 *
 * IN:
 * @st_ic	Array for per-CPU statistics.
 * @ic_nr	Number of interrupts (hard or soft) per CPU.
 * @dis		TRUE if a header line must be printed.
 * @itv		Interval value.
 * @prev	Position in array where statistics used	as reference are.
 *		Stats used as reference may be the previous ones read, or
 *		the very first ones when calculating the average.
 * @curr	Position in array where current statistics will be saved.
 * @prev_string	String displayed at the beginning of a header line. This is
 * 		the timestamp of the previous sample, or "Average" when
 * 		displaying average stats.
 * @curr_string	String displayed at the beginning of current sample stats.
 * 		This is the timestamp of the current sample, or "Average"
 * 		when displaying average stats.
 * @tab		Number of tabs to print (JSON format only).
 * @next	TRUE is a previous activity has been displayed (JSON format
 * 		only).
 * @type	Activity (M_D_IRQ_CPU or M_D_SOFTIRQS).
 ***************************************************************************
 */
void write_irqcpu_stats(struct stats_irqcpu *st_ic[], int ic_nr, int dis,
			unsigned long long itv, int prev, int curr,
			char *prev_string, char *curr_string, int tab,
			int *next, int type)
{
	if (DISPLAY_JSON_OUTPUT(flags)) {
		if (*next) {
			printf(",\n");
		}
		*next = TRUE;
		write_json_irqcpu_stats(tab, st_ic, ic_nr, itv, prev, curr, type);
	}
	else {
		write_plain_irqcpu_stats(st_ic, ic_nr, dis, itv, prev, curr,
					 prev_string, curr_string);
	}
}

/*
 ***************************************************************************
 * Core function used to display statistics.
 *
 * IN:
 * @prev	Position in array where statistics used	as reference are.
 *		Stats used as reference may be the previous ones read, or
 *		the very first ones when calculating the average.
 * @curr	Position in array where statistics for current sample are.
 * @dis		TRUE if a header line must be printed.
 * @prev_string	String displayed at the beginning of a header line. This is
 * 		the timestamp of the previous sample, or "Average" when
 * 		displaying average stats.
 * @curr_string	String displayed at the beginning of current sample stats.
 * 		This is the timestamp of the current sample, or "Average"
 * 		when displaying average stats.
 ***************************************************************************
 */
void write_stats_core(int prev, int curr, int dis,
		      char *prev_string, char *curr_string)
{
	struct stats_cpu *scc, *scp;
	unsigned long long itv, g_itv;
	int cpu, tab = 4, next = FALSE;

	/* Test stdout */
	TEST_STDOUT(STDOUT_FILENO);

	if (DISPLAY_JSON_OUTPUT(flags)) {
		xprintf(tab++, "{");
		xprintf(tab, "\"timestamp\": \"%s\",", curr_string);
	}

	/* Compute time interval */
	g_itv = get_interval(uptime[prev], uptime[curr]);

	/* Reduce interval value to one processor */
	if (cpu_nr > 1) {
		itv = get_interval(uptime0[prev], uptime0[curr]);
	}
	else {
		itv = g_itv;
	}

	/* Print CPU stats */
	if (DISPLAY_CPU(actflags)) {
		write_cpu_stats(dis, g_itv, prev, curr, prev_string, curr_string,
				tab, &next);
	}

	/* Print node CPU stats */
	if (DISPLAY_NODE(actflags)) {
		write_node_stats(dis, g_itv, itv, prev, curr, prev_string, curr_string,
				 tab, &next);
	}

	/* Print total number of interrupts per processor */
	if (DISPLAY_IRQ_SUM(actflags)) {
		write_isumcpu_stats(dis, itv, prev, curr, prev_string, curr_string,
				    tab, &next);
	}

	/* Display each interrupt value for each CPU */
	if (DISPLAY_IRQ_CPU(actflags)) {
		write_irqcpu_stats(st_irqcpu, irqcpu_nr, dis, itv, prev, curr,
				   prev_string, curr_string, tab, &next, M_D_IRQ_CPU);
	}
	if (DISPLAY_SOFTIRQS(actflags)) {
		write_irqcpu_stats(st_softirqcpu, softirqcpu_nr, dis, itv, prev, curr,
				   prev_string, curr_string, tab, &next, M_D_SOFTIRQS);
	}

	if (DISPLAY_JSON_OUTPUT(flags)) {
		printf("\n");
		xprintf0(--tab, "}");
	}

	/* Fix CPU counter values for every offline CPU */
	for (cpu = 1; cpu <= cpu_nr; cpu++) {

		scc = st_cpu[curr] + cpu;
		scp = st_cpu[prev] + cpu;

		if ((scc->cpu_user    + scc->cpu_nice + scc->cpu_sys   +
		     scc->cpu_iowait  + scc->cpu_idle + scc->cpu_steal +
		     scc->cpu_hardirq + scc->cpu_softirq) == 0) {
			/*
			 * Offline CPU found.
			 * Set current struct fields (which have been set to zero)
			 * to values from previous iteration. Hence their values won't
			 * jump from zero when the CPU comes back online.
			 */
			*scc = *scp;
		}
	}
}

/*
 ***************************************************************************
 * Print statistics average.
 *
 * IN:
 * @curr	Position in array where statistics for current sample are.
 * @dis		TRUE if a header line must be printed.
 ***************************************************************************
 */
void write_stats_avg(int curr, int dis)
{
	char string[16];

	strncpy(string, _("Average:"), 16);
	string[15] = '\0';
	write_stats_core(2, curr, dis, string, string);
}

/*
 ***************************************************************************
 * Print statistics.
 *
 * IN:
 * @curr	Position in array where statistics for current sample are.
 * @dis		TRUE if a header line must be printed.
 ***************************************************************************
 */
void write_stats(int curr, int dis)
{
	char cur_time[2][TIMESTAMP_LEN];

	/* Get previous timestamp */
	if (is_iso_time_fmt()) {
		strftime(cur_time[!curr], sizeof(cur_time[!curr]), "%H:%M:%S", &mp_tstamp[!curr]);
	}
	else {
		strftime(cur_time[!curr], sizeof(cur_time[!curr]), "%X", &(mp_tstamp[!curr]));
	}

	/* Get current timestamp */
	if (is_iso_time_fmt()) {
		strftime(cur_time[curr], sizeof(cur_time[curr]), "%H:%M:%S", &mp_tstamp[curr]);
	}
	else {
		strftime(cur_time[curr], sizeof(cur_time[curr]), "%X", &(mp_tstamp[curr]));
	}

	write_stats_core(!curr, curr, dis, cur_time[!curr], cur_time[curr]);
}

/*
 ***************************************************************************
 * Read stats from /proc/interrupts or /proc/softirqs.
 *
 * IN:
 * @file	/proc file to read (interrupts or softirqs).
 * @ic_nr	Number of interrupts (hard or soft) per CPU.
 * @curr	Position in array where current statistics will be saved.
 *
 * OUT:
 * @st_ic	Array for per-CPU interrupts statistics.
 ***************************************************************************
 */
void read_interrupts_stat(char *file, struct stats_irqcpu *st_ic[], int ic_nr, int curr)
{
	FILE *fp;
	struct stats_irq *st_irq_i;
	struct stats_irqcpu *p;
	char *line = NULL, *li;
	unsigned long irq = 0;
	unsigned int cpu;
	int cpu_index[cpu_nr], index = 0, len;
	char *cp, *next;

	/* Reset total number of interrupts received by each CPU */
	for (cpu = 0; cpu < cpu_nr; cpu++) {
		st_irq_i = st_irq[curr] + cpu + 1;
		st_irq_i->irq_nr = 0;
	}

	if ((fp = fopen(file, "r")) != NULL) {

		SREALLOC(line, char, INTERRUPTS_LINE + 11 * cpu_nr);

		/*
		 * Parse header line to see which CPUs are online
		 */
		while (fgets(line, INTERRUPTS_LINE + 11 * cpu_nr, fp) != NULL) {
			next = line;
			while (((cp = strstr(next, "CPU")) != NULL) && (index < cpu_nr)) {
				cpu = strtol(cp + 3, &next, 10);
				cpu_index[index++] = cpu;
			}
			if (index)
				/* Header line found */
				break;
		}

		/* Parse each line of interrupts statistics data */
		while ((fgets(line, INTERRUPTS_LINE + 11 * cpu_nr, fp) != NULL) &&
		       (irq < ic_nr)) {

			/* Skip over "<irq>:" */
			if ((cp = strchr(line, ':')) == NULL)
				/* Chr ':' not found */
				continue;
			cp++;

			p = st_ic[curr] + irq;

			/* Remove possible heading spaces in interrupt's name... */
			li = line;
			while (*li == ' ')
				li++;

			len = strcspn(li, ":");
			if (len >= MAX_IRQ_LEN) {
				len = MAX_IRQ_LEN - 1;
			}
			/* ...then save its name */
			strncpy(p->irq_name, li, len);
			p->irq_name[len] = '\0';

			/* For each interrupt: Get number received by each CPU */
			for (cpu = 0; cpu < index; cpu++) {
				p = st_ic[curr] + cpu_index[cpu] * ic_nr + irq;
				st_irq_i = st_irq[curr] + cpu_index[cpu] + 1;
				/*
				 * No need to set (st_irqcpu + cpu * irqcpu_nr)->irq_name:
				 * This is the same as st_irqcpu->irq_name.
				 * Now save current interrupt value for current CPU (in stats_irqcpu structure)
				 * and total number of interrupts received by current CPU (in stats_irq structure).
				 */
				p->interrupt = strtoul(cp, &next, 10);
				st_irq_i->irq_nr += p->interrupt;
				cp = next;
			}
			irq++;
		}

		fclose(fp);

		free(line);
	}

	while (irq < ic_nr) {
		/* Nb of interrupts per processor has changed */
		p = st_ic[curr] + irq;
		p->irq_name[0] = '\0';	/* This value means this is a dummy interrupt */
		irq++;
	}
}

/*
 ***************************************************************************
 * Main loop: Read stats from the relevant sources, and display them.
 *
 * IN:
 * @dis_hdr	Set to TRUE if the header line must always be printed.
 * @rows	Number of rows of screen.
 ***************************************************************************
 */
void rw_mpstat_loop(int dis_hdr, int rows)
{
	struct stats_cpu *scc;
	int cpu;
	int curr = 1, dis = 1;
	unsigned long lines = rows;

	/* Dont buffer data if redirected to a pipe */
	setbuf(stdout, NULL);

	/* Read uptime and CPU stats */
	if (cpu_nr > 1) {
		/*
		 * Init uptime0. So if /proc/uptime cannot fill it,
		 * this will be done by /proc/stat.
		 */
		uptime0[0] = 0;
		read_uptime(&(uptime0[0]));
	}
	read_stat_cpu(st_cpu[0], cpu_nr + 1, &(uptime[0]), &(uptime0[0]));
	if (DISPLAY_NODE(actflags)) {
		set_node_cpu_stats(st_node[0], st_cpu[0]);
	}

	/*
	 * Read total number of interrupts received among all CPU.
	 * (this is the first value on the line "intr:" in the /proc/stat file).
	 */
	if (DISPLAY_IRQ_SUM(actflags)) {
		read_stat_irq(st_irq[0], 1);
	}

	/*
	 * Read number of interrupts received by each CPU, for each interrupt,
	 * and compute the total number of interrupts received by each CPU.
	 */
	if (DISPLAY_IRQ_SUM(actflags) || DISPLAY_IRQ_CPU(actflags)) {
		/* Read this file to display int per CPU or total nr of int per CPU */
		read_interrupts_stat(INTERRUPTS, st_irqcpu, irqcpu_nr, 0);
	}
	if (DISPLAY_SOFTIRQS(actflags)) {
		read_interrupts_stat(SOFTIRQS, st_softirqcpu, softirqcpu_nr, 0);
	}

	if (!interval) {
		/* Display since boot time */
		mp_tstamp[1] = mp_tstamp[0];
		memset(st_cpu[1], 0, STATS_CPU_SIZE * (cpu_nr + 1));
		memset(st_node[1], 0, STATS_CPU_SIZE * (cpu_nr + 1));
		memset(st_irq[1], 0, STATS_IRQ_SIZE * (cpu_nr + 1));
		memset(st_irqcpu[1], 0, STATS_IRQCPU_SIZE * (cpu_nr + 1) * irqcpu_nr);
		if (DISPLAY_SOFTIRQS(actflags)) {
			memset(st_softirqcpu[1], 0, STATS_IRQCPU_SIZE * (cpu_nr + 1) * softirqcpu_nr);
		}
		write_stats(0, DISP_HDR);
		if (DISPLAY_JSON_OUTPUT(flags)) {
			printf("\n\t\t\t]\n\t\t}\n\t]\n}}\n");
		}
		exit(0);
	}

	/* Set a handler for SIGALRM */
	memset(&alrm_act, 0, sizeof(alrm_act));
	alrm_act.sa_handler = alarm_handler;
	sigaction(SIGALRM, &alrm_act, NULL);
	alarm(interval);

	/* Save the first stats collected. Will be used to compute the average */
	mp_tstamp[2] = mp_tstamp[0];
	uptime[2] = uptime[0];
	uptime0[2] = uptime0[0];
	memcpy(st_cpu[2], st_cpu[0], STATS_CPU_SIZE * (cpu_nr + 1));
	memcpy(st_node[2], st_node[0], STATS_CPU_SIZE * (cpu_nr + 1));
	memcpy(st_irq[2], st_irq[0], STATS_IRQ_SIZE * (cpu_nr + 1));
	memcpy(st_irqcpu[2], st_irqcpu[0], STATS_IRQCPU_SIZE * (cpu_nr + 1) * irqcpu_nr);
	if (DISPLAY_SOFTIRQS(actflags)) {
		memcpy(st_softirqcpu[2], st_softirqcpu[0],
		       STATS_IRQCPU_SIZE * (cpu_nr + 1) * softirqcpu_nr);
	}

	if (!DISPLAY_JSON_OUTPUT(flags)) {
		/* Set a handler for SIGINT */
		memset(&int_act, 0, sizeof(int_act));
		int_act.sa_handler = int_handler;
		sigaction(SIGINT, &int_act, NULL);
	}

	pause();

	if (sigint_caught)
		/* SIGINT signal caught during first interval: Exit immediately */
		return;

	do {
		/*
		 * Resetting the structure not needed since every fields will be set.
		 * Exceptions are per-CPU structures: Some of them may not be filled
		 * if corresponding processor is disabled (offline). We set them to zero
		 * to be able to distinguish between offline and tickless CPUs.
		 */
		for (cpu = 1; cpu <= cpu_nr; cpu++) {
			scc = st_cpu[curr] + cpu;
			memset(scc, 0, STATS_CPU_SIZE);
			scc = st_node[curr] + cpu;
			memset(scc, 0, STATS_CPU_SIZE);
		}

		/* Get time */
		get_localtime(&(mp_tstamp[curr]), 0);

		/* Read uptime and CPU stats */
		if (cpu_nr > 1) {
			uptime0[curr] = 0;
			read_uptime(&(uptime0[curr]));
		}
		read_stat_cpu(st_cpu[curr], cpu_nr + 1, &(uptime[curr]), &(uptime0[curr]));
		if (DISPLAY_NODE(actflags)) {
			set_node_cpu_stats(st_node[curr], st_cpu[curr]);
		}

		/* Read total number of interrupts received among all CPU */
		if (DISPLAY_IRQ_SUM(actflags)) {
			read_stat_irq(st_irq[curr], 1);
		}

		/*
		 * Read number of interrupts received by each CPU, for each interrupt,
		 * and compute the total number of interrupts received by each CPU.
		 */
		if (DISPLAY_IRQ_SUM(actflags) || DISPLAY_IRQ_CPU(actflags)) {
			read_interrupts_stat(INTERRUPTS, st_irqcpu, irqcpu_nr, curr);
		}
		if (DISPLAY_SOFTIRQS(actflags)) {
			read_interrupts_stat(SOFTIRQS, st_softirqcpu, softirqcpu_nr, curr);
		}

		/* Write stats */
		if (!dis_hdr) {
			dis = lines / rows;
			if (dis) {
				lines %= rows;
			}
			lines++;
		}
		write_stats(curr, dis);

		if (count > 0) {
			count--;
		}

		if (count) {

			if (DISPLAY_JSON_OUTPUT(flags)) {
				printf(",\n");
			}
			pause();

			if (sigint_caught) {
				/* SIGINT signal caught => Display average stats */
				count = 0;
				printf("\n");	/* Skip "^C" displayed on screen */
			}
			else {
				curr ^= 1;
			}
		}
	}
	while (count);

	/* Write stats average */
	if (DISPLAY_JSON_OUTPUT(flags)) {
		printf("\n\t\t\t]\n\t\t}\n\t]\n}}\n");
	}
	else {
		write_stats_avg(curr, dis_hdr);
	}
}

/*
 ***************************************************************************
 * Main entry to the program
 ***************************************************************************
 */
int main(int argc, char **argv)
{
	int opt = 0, i, actset = FALSE;
	struct utsname header;
	int dis_hdr = -1;
	int rows = 23;
	char *t;

#ifdef USE_NLS
	/* Init National Language Support */
	init_nls();
#endif

	/* Init color strings */
	init_colors();

	/* Get HZ */
	get_HZ();

	/* What is the highest processor number on this machine? */
	cpu_nr = get_cpu_nr(~0, TRUE);

	/* Calculate number of interrupts per processor */
	irqcpu_nr = get_irqcpu_nr(INTERRUPTS, NR_IRQS, cpu_nr) +
		    NR_IRQCPU_PREALLOC;
	/* Calculate number of soft interrupts per processor */
	softirqcpu_nr = get_irqcpu_nr(SOFTIRQS, NR_IRQS, cpu_nr) +
			NR_IRQCPU_PREALLOC;

	/*
	 * cpu_nr: a value of 2 means there are 2 processors (0 and 1).
	 * In this case, we have to allocate 3 structures: global, proc0 and proc1.
	 */
	salloc_mp_struct(cpu_nr + 1);

	/* Get NUMA node placement */
	node_nr = get_node_placement(cpu_nr, cpu_per_node, cpu2node);

	while (++opt < argc) {

		if (!strcmp(argv[opt], "-I")) {
			if (argv[++opt]) {
				actset = TRUE;

				for (t = strtok(argv[opt], ","); t; t = strtok(NULL, ",")) {
					if (!strcmp(t, K_SUM)) {
						/* Display total number of interrupts per CPU */
						actflags |= M_D_IRQ_SUM;
					}
					else if (!strcmp(t, K_CPU)) {
						/* Display interrupts per CPU */
						actflags |= M_D_IRQ_CPU;
					}
					else if (!strcmp(t, K_SCPU)) {
						/* Display soft interrupts per CPU */
						actflags |= M_D_SOFTIRQS;
					}
					else if (!strcmp(t, K_ALL)) {
						actflags |= M_D_IRQ_SUM + M_D_IRQ_CPU + M_D_SOFTIRQS;
					}
					else {
						usage(argv[0]);
					}
				}
			}
			else {
				usage(argv[0]);
			}
		}

		else if (!strcmp(argv[opt], "-o")) {
			/* Select output format */
			if (argv[++opt] && !strcmp(argv[opt], K_JSON)) {
				flags |= F_JSON_OUTPUT;
			}
			else {
				usage(argv[0]);
			}
		}

		else if (!strcmp(argv[opt], "-N")) {
			if (argv[++opt]) {
				if (node_nr >= 0) {
					flags |= F_N_OPTION;
					actflags |= M_D_NODE;
					actset = TRUE;
					dis_hdr = 9;
					if (parse_values(argv[opt], node_bitmap, node_nr + 1, K_LOWERALL)) {
						usage(argv[0]);
					}
				}
			}
			else {
				usage(argv[0]);
			}
		}

		else if (!strcmp(argv[opt], "-P")) {
			/* '-P ALL' can be used on UP machines */
			if (argv[++opt]) {
				flags |= F_P_OPTION;
				dis_hdr = 9;

				if (!strcmp(argv[opt], K_ON)) {
					/* Display stats for all online CPU */
					flags |= F_P_ON;
					memset(cpu_bitmap, ~0, BITMAP_SIZE(cpu_nr));
				}
				else if (parse_values(argv[opt], cpu_bitmap, cpu_nr, K_LOWERALL)) {
					usage(argv[0]);
				}
			}
			else {
				usage(argv[0]);
			}
		}

		else if (!strncmp(argv[opt], "-", 1)) {
			for (i = 1; *(argv[opt] + i); i++) {

				switch (*(argv[opt] + i)) {

				case 'A':
					actflags |= M_D_CPU + M_D_IRQ_SUM + M_D_IRQ_CPU + M_D_SOFTIRQS;
					if (node_nr >= 0) {
						actflags |= M_D_NODE;
						flags |= F_N_OPTION;
						memset(node_bitmap, 0xff, ((cpu_nr + 1) >> 3) + 1);
					}
					actset = TRUE;
					/* Select all processors */
					flags |= F_P_OPTION;
					memset(cpu_bitmap, 0xff, ((cpu_nr + 1) >> 3) + 1);
					break;

				case 'n':
					/* Display CPU stats based on NUMA node placement */
					if (node_nr >= 0) {
						actflags |= M_D_NODE;
						actset = TRUE;
					}
					break;

				case 'u':
					/* Display CPU */
					actflags |= M_D_CPU;
					break;

				case 'V':
					/* Print version number */
					print_version();
					break;

				default:
					usage(argv[0]);
				}
			}
		}

		else if (interval < 0) {
			/* Get interval */
			if (strspn(argv[opt], DIGITS) != strlen(argv[opt])) {
				usage(argv[0]);
			}
			interval = atol(argv[opt]);
			if (interval < 0) {
				usage(argv[0]);
			}
			count = -1;
		}

		else if (count <= 0) {
			/* Get count value */
			if ((strspn(argv[opt], DIGITS) != strlen(argv[opt])) ||
			    !interval) {
				usage(argv[0]);
			}
			count = atol(argv[opt]);
			if (count < 1) {
				usage(argv[0]);
			}
		}

		else {
			usage(argv[0]);
		}
	}

	/* Default: Display CPU (e.g., "mpstat", "mpstat -P 1", "mpstat -P 1 -n", "mpstat -P 1 -N 1"... */
	if (!actset ||
	    (USE_P_OPTION(flags) && !(actflags & ~M_D_NODE))) {
		actflags |= M_D_CPU;
	}

	if (count_bits(&actflags, sizeof(unsigned int)) > 1) {
		dis_hdr = 9;
	}

	if (!USE_P_OPTION(flags)) {
		/* Option -P not used: Set bit 0 (global stats among all proc) */
		*cpu_bitmap = 1;
	}
	if (!USE_N_OPTION(flags)) {
		/* Option -N not used: Set bit 0 (global stats among all nodes) */
		*node_bitmap = 1;
	}
	if (dis_hdr < 0) {
		dis_hdr = 0;
	}
	if (!dis_hdr) {
		/* Get window size */
		rows = get_win_height();
	}
	if (interval < 0) {
		/* Interval not set => display stats since boot time */
		interval = 0;
	}

	if (DISPLAY_JSON_OUTPUT(flags)) {
		/* Use a decimal point to make JSON code compliant with RFC7159 */
		setlocale(LC_NUMERIC, "C");
	}

	/* Get time */
	get_localtime(&(mp_tstamp[0]), 0);

	/* Get system name, release number and hostname */
	uname(&header);
	print_gal_header(&(mp_tstamp[0]), header.sysname, header.release,
			 header.nodename, header.machine, get_cpu_nr(~0, FALSE),
			 DISPLAY_JSON_OUTPUT(flags));

	/* Main loop */
	rw_mpstat_loop(dis_hdr, rows);

	/* Free structures */
	sfree_mp_struct();

	return 0;
}
