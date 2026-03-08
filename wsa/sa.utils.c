/*
 * sa.seeds.c

 * This module is part of the sortalign package
 * A new RNA aligner with emphasis on parallelisation by multithreading and channels, and memory locality
 * Authors: Jean Thierry-Mieg, Danielle Thierry-Mieg and Greg Boratyn, NCBI/NLM/NIH
 * Created April 18, 2025

 * This is public.


 * This module analyses the hardware
 * to bing the aligner to the least buzy core
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>  // sysconf, getpid, usleep, access, etc.

#ifdef __linux__
#include <dirent.h>
#include <sys/types.h>
#include <time.h>         // time(NULL)
#endif

#ifdef __APPLE__
#include <mach/mach.h>
#include <sys/sysctl.h>
#endif

#ifdef __linux__
/* ==================== LINUX ONLY ==================== */

/*
  #include <sys/sysinfo.h>  // for get_nprocs_conf() alternative if needed

#include <stdlib.h>       // random, srandom

*/
#define MAX_CPUS 256  // Adjust if your machine has more

typedef struct {
    unsigned long long user, nice, sys, idle, iowait, irq, softirq, steal, guest, guest_nice ;
} cpu_times_t ;

// Function to read /proc/stat into per-CPU times array (returns num_cpus read)#include <stdio.h>
#include <sys/resource.h>

int get_max_threads_limit (void)
{
    struct rlimit rl;

    if (getrlimit (RLIMIT_NPROC, &rl) == 0)
      {
        if (rl.rlim_cur == RLIM_INFINITY)
	  return -1 ;  // "unlimited"
        else
	  return (int)rl.rlim_cur ;    //soft limit
      }
    return 0 ;   // error
}


static int read_proc_stat(cpu_times_t *times)
{
    FILE *f = fopen ("/proc/stat", "r") ;
    if (!f) return -1 ;

    char buf[2048] = {0} ;
    int num_cpus = 0 ;

    // Read at least one line (the aggregate "cpu " line)
    if (fgets(buf, sizeof(buf), f)) {
        // Optionally skip it, or parse if you want aggregate stats
    }

    // Now read per-CPU lines
    while (fgets (buf, sizeof(buf), f))
      {
	int cpu_id = -1;
	cpu_times_t tt = {0};
	
	int n = sscanf (buf, "cpu%d %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
		       &cpu_id,
                       &tt.user, &tt.nice, &tt.sys, &tt.idle,
                       &tt.iowait, &tt.irq, &tt.softirq, &tt.steal,
                       &tt.guest, &tt.guest_nice);
	
        if (n >= 9 && cpu_id >= 0 && cpu_id < MAX_CPUS) {
	  times[cpu_id] = tt;
	  if (cpu_id + 1 > num_cpus) num_cpus = cpu_id + 1;
        }
      }
    
  fclose(f) ;
  return num_cpus ;
}
  
/* node with least average CPU utilization */
int saBestNumactlNode (int *maxThreadsp)
{
  int best_node = 0 ;
  double min_avg_usage = -1.0 ;

  // First sample
  cpu_times_t before[MAX_CPUS] = {0} ;
  int num_cpus = read_proc_stat(before) ;
  if (num_cpus <= 1) // no choice
    return 0 ;      
  
  usleep(500000) ;  // 0.5s delay for delta (tune if needed)
  srandom(time(NULL) ^ getpid()) ;
  best_node = random() % num_cpus ; /* random fallback */

  
  // Second sample
  cpu_times_t after[MAX_CPUS] = {0} ;
  if (read_proc_stat(after) != num_cpus)
    return best_node ;
  
  
  // Compute per-CPU utilization %
  double usage[MAX_CPUS] = {0.0} ;
  for (int cpu = 0 ; cpu < num_cpus ; cpu++) {
    unsigned long long delta_user = after[cpu].user - before[cpu].user ;
    unsigned long long delta_nice = after[cpu].nice - before[cpu].nice ;
    unsigned long long delta_sys = after[cpu].sys - before[cpu].sys ;
    unsigned long long delta_idle = after[cpu].idle - before[cpu].idle ;
    unsigned long long delta_iowait = after[cpu].iowait - before[cpu].iowait ;
    unsigned long long delta_irq = after[cpu].irq - before[cpu].irq ;
    unsigned long long delta_softirq = after[cpu].softirq - before[cpu].softirq ;
    unsigned long long delta_steal = after[cpu].steal - before[cpu].steal ;

    unsigned long long delta_total = delta_user + delta_nice + delta_sys + delta_idle +
      delta_iowait + delta_irq + delta_softirq + delta_steal ;
    if (delta_total == 0) continue ;  // Avoid div0
    
    unsigned long long delta_active = delta_user + delta_nice + delta_sys +
      delta_irq + delta_softirq + delta_steal ;
    usage[cpu] = 100.0 * ((double)delta_active / (double)delta_total) ;
  }
  
  // Now loop over nodes
  DIR *d = opendir("/sys/devices/system/node") ;
  if (d) {
    struct dirent *e ;
    while ((e = readdir(d))) {
      int node ;
      if (sscanf(e->d_name, "node%d", &node) != 1) continue ;
      
      // Get cpumap for this node (your original way)
      char path[64] ;
      snprintf(path, sizeof(path), "/sys/devices/system/node/node%d/cpumap", node) ;
      FILE *f = fopen(path, "r") ;
      if (!f) continue ;
      unsigned long long map = 0 ;
      int n = fscanf(f, "%llx", &map) ;
      fclose(f) ;
      if (n != 1) continue ;
      
      // Compute avg usage for CPUs in this node
      double node_usage_sum = 0.0 ;
      int node_cpu_count = 0 ;
      for (int cpu = 0 ; cpu < num_cpus && cpu < 64 ; cpu++) {  // Assumes map fits ull
	if (map & (1ULL << cpu)) {
	  node_usage_sum += usage[cpu] ;
	  node_cpu_count++ ;
	}
      }
      if (node_cpu_count == 0) continue ;
      
      double avg_usage = node_usage_sum / node_cpu_count ;
      
      if (min_avg_usage == -1.0 || avg_usage < min_avg_usage)
	{
	  min_avg_usage = avg_usage ;
	  best_node = node ;
	}
    }
    closedir(d) ;
  } 

  *maxThreadsp = get_max_threads_limit () ;
  return best_node ;
}
#endif
/* ==================== END LINUX ONLY ==================== */
#ifdef JUNK
#include <sys/resource.h>

/* peak process memory on linux, average on mac */
long get_current_rss_kb(void) {
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
        return ru.ru_maxrss;  // KB on Linux/macOS
    }
    return -1;
}


/* Returns available RAM (in KB) on the current NUMA node, or system total if no NUMA */
long get_available_ram_kb(void) {
    long free_kb = 0, cached_kb = 0, inactive_kb = 0;

#ifdef __linux__
    // Find current NUMA node from /proc/self/status (Mems_allowed_list)
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) goto system_fallback;

    char line[256];
    int node = -1;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "Mems_allowed_list:", 18) == 0) {
            sscanf(line + 18, "%d", &node);
            break;
        }
    }
    fclose(f);
    if (node < 0) goto system_fallback;

    // Now read meminfo for this node
    char path[128];
    snprintf(path, sizeof(path), "/sys/devices/system/node/node%d/meminfo", node);
    f = fopen(path, "r");
    if (!f) goto system_fallback;

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "Node %*d MemFree:", 17) == 0) {
            sscanf(line + 17, "%ld kB", &free_kb);
        } else if (strncmp(line, "Node %*d Inactive:", 18) == 0) {
            sscanf(line + 18, "%ld kB", &inactive_kb);
        } else if (strncmp(line, "Node %*d Cached:", 16) == 0) {
            sscanf(line + 16, "%ld kB", &cached_kb);
        }
    }
    fclose(f);

    if (free_kb > 0) {
        return free_kb + cached_kb + inactive_kb;  // Reclaimable available
    }
#endif

system_fallback:
    // Fallback: system-wide available (from /proc/meminfo on Linux, or sysconf on others)
    long pages = sysconf(_SC_AVPHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    return (pages * page_size) / 1024;  // KB
}
#endif
/* Returns estimated available RAM in KB on current node (Linux) or total RAM (macOS) */
long get_available_ram_kb(void)
{
#ifdef __linux__
    // Try per-node memory first (when running under numactl)
    FILE *f = fopen("/proc/self/status", "r");
    if (f) {
        char line[256];
        int node = -1;
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "Mems_allowed_list:", 18) == 0) {
                sscanf(line + 18, "%d", &node);
                break;
            }
        }
        fclose(f);

        if (node >= 0) {
            char path[128];
            snprintf(path, sizeof(path), "/sys/devices/system/node/node%d/meminfo", node);
            f = fopen(path, "r");
            if (f) {
                long free_kb = 0, cached = 0, inactive = 0;
                while (fgets(line, sizeof(line), f)) {
                    if (strstr(line, "MemFree:"))     sscanf(line, "%*s %ld", &free_kb);
                    if (strstr(line, "Cached:"))      sscanf(line, "%*s %ld", &cached);
                    if (strstr(line, "Inactive:"))    sscanf(line, "%*s %ld", &inactive);
                }
                fclose(f);
                if (free_kb > 0)
                    return free_kb + cached + inactive;
            }
        }
    }

    // Linux system-wide fallback
    f = fopen("/proc/meminfo", "r");
    if (f) {
        char line[256];
        long avail = 0;
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "MemAvailable:", 13) == 0) {
                sscanf(line + 13, "%ld", &avail);
                break;
            }
        }
        fclose(f);
        if (avail > 0) return avail;
    }

#elif defined(__APPLE__)
    // macOS: Get total physical RAM and return ~70% as "available"
    int64_t total_bytes = 0;
    size_t len = sizeof(total_bytes);
    if (sysctlbyname("hw.memsize", &total_bytes, &len, NULL, 0) == 0) {
        return (long)(total_bytes / 1024LL * 70 / 100);   // conservative 70%
    }
#endif

    // Final fallback using sysconf (works on both, but less accurate on macOS)
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && page_size > 0) {
        return (pages * page_size) / 1024;
    }

    return 1024 * 1024;   // 1 GB safety fallback
}

#ifdef JUNK
In your dripper: Before launching a new block, check:
  In your dripper: Before launching a new block, check:

  long avail_kb = get_available_ram_kb();
long projected_kb = avail_kb * 0.8;  // 80% threshold
long block_ram_estimate = 20LL * 1024 * 1024;  // e.g., 20 GB per block in KB

if (current_blocks < N && get_current_rss_kb() + block_ram_estimate < projected_kb) {
    // Safe to drip new block
} else {
    usleep(100000);  // Wait and retry every 0.1 s
}

  This auto-balances: On multi-node (e.g., 4x24 CPU, assume 128 GB/node),
    it caps blocks to fit RAM. On single-node (4 CPU, say 32 GB total),
    it naturally limits to fewer blocks. Tune estimates/thresholds based on tests.
#endif
    

int get_number_of_cpus(void)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return (n > 0) ? (int)n : 1;
}

int get_number_of_cpus_per_node (void)
{
    /* First, find which NUMA node we are currently running on */
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) goto fallback;

    char line[256];
    int current_node = 0;

    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "Mems_allowed_list: %d", &current_node) == 1) {
            break;
        }
    }
    fclose(f);

    /* Now count how many CPUs belong to this node */
    char path[128];
    snprintf(path, sizeof(path), "/sys/devices/system/node/node%d/cpumap", current_node);

    f = fopen(path, "r");
    if (!f) goto fallback;

    unsigned long long map = 0;
    if (fscanf(f, "%llx", &map) == 1) {
        fclose(f);
        return __builtin_popcountll(map);   // count the number of set bits
    }
    fclose(f);

fallback:
    /* Fallback: return total CPUs on the machine */
    long int n = sysconf(_SC_NPROCESSORS_ONLN);
    return (n > 0) ? (int)n : 1;
}

#ifdef JUNK

foreach ii (1 2 3 4)
  \rm -rf titi$ii ; /usr/bin/time -f "TIMING E %E U %U M %M P %P" ~/ace/bin.LINUX_4_OPT/sortalign -x Aligners/011_SortAlignG6R3/IDX.GRCh38.18.81 -i Fasta/iRefSeq38/iRefSeq38.fasta.gz --align -o titi$ii >& titi$ii.err &
sleep 4
end

#endif

#ifdef JUNK

#endif

