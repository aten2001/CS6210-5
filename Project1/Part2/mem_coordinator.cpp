#include <libvirt/libvirt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define HYPERV_URI	"qemu:///system"

static virDomainPtr *domains = NULL;

struct virDomainWindow
{
	virDomainPtr domain;
	//n*Stats represets number of valid stats.
	int nPrevStats;
	int nCurrStats;
	//historical and current memory stats
	virDomainMemoryStatStruct prevStats[VIR_DOMAIN_MEMORY_STAT_NR];
	virDomainMemoryStatStruct currStats[VIR_DOMAIN_MEMORY_STAT_NR];
};

//just need total memory and free memory. Everything else can be calculated
struct hostMemoryStat
{
	unsigned long long total;
	unsigned long long free;
};

enum usageCategories
{
	COLD = 0,
	HOT,
	WARM	//This category is ideal
};

static const unsigned int percentThreshold [] =
{
	25,	//COLD
	80	//HOT
};

//static const unsigned int majorFaultThreshold [] =
//{
	

//string mapping to virDomainMemoryStatTags
static const char * tagMap [] = 
{
	"swap_in", 	//VIR_DOMAIN_MEMORY_STAT_SWAP_IN
	"swap_out",	//VIR_DOMAIN_MEMORY_STAT_SWAP_OUT
	"major_fault",	//VIR_DOMAIN_MEMORY_STAT_MAJOR_FAULT
	"minor_fault",	//VIR_DOMAIN_MEMORY_STAT_MINOR_FAULT
	"unused",	//VIR_DOMAIN_MEMORY_STAT_UNUSED
	"available",	//VIR_DOMAIN_MEMORY_STAT_AVAILABLE
	"balloon",	//VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON
	"rss",		//VIR_DOMAIN_MEMORY_STAT_RSS
};

int initDomainWindows(virConnectPtr connection, virDomainWindow *&domainWindows)
{
	unsigned int flags = VIR_CONNECT_LIST_DOMAINS_RUNNING | VIR_CONNECT_LIST_DOMAINS_PERSISTENT;
	int nDomains = 0;

	if((nDomains = virConnectListAllDomains(connection, &domains, flags)) < 0)
	{
		printf("Error getting domains list.\n");
		return -1;
	}

	domainWindows = (virDomainWindow*)calloc(nDomains, sizeof(virDomainWindow));
	printf("Number of domains: %d\n", nDomains);

	for (int i = 0; i < nDomains; i++)
	{
		domainWindows[i].domain = domains[i];
	}

	return nDomains;
}

int fetchStats(int nDomains, virDomainWindow *domainWindows)
{
	printf("Fetching memory stats\n");
	for (int i = 0; i < nDomains; i ++)
	{
		//copy last stats into previous stats to compare
		memcpy(domainWindows[i].prevStats, domainWindows[i].currStats, VIR_DOMAIN_MEMORY_STAT_NR * sizeof(virDomainMemoryStatStruct));
		domainWindows[i].nPrevStats = domainWindows[i].nCurrStats;	//should always be the same, but lets be safe
		if ((domainWindows[i].nCurrStats = virDomainMemoryStats(domainWindows[i].domain, domainWindows[i].currStats, VIR_DOMAIN_MEMORY_STAT_NR, 0)) < 0)
		{
			printf("Error retrieving memory stats. Aborting.\n");
			return -1;
		}
		
	}
}

//helper function - get requested stat from the virDomainMemoryStatPtr.
//This function isn't optimized for performance, as it's not required, so 
//it will just go through the struct to find the tag and return the pointer to the struct.
virDomainMemoryStatPtr getStatPtr(int nParams, virDomainMemoryStatPtr stats, int tag)
{
	for (int i = 0; i < nParams; i++)
	{
		if (stats[i].tag == tag)
		{
			return &stats[i];
		}
	}
	return NULL;
}

unsigned long long getDomainBalloonTotal(int nDomains, virDomainWindow *domainWindows)
{
	unsigned long long total = 0;
	virDomainMemoryStatPtr temp = NULL;
	for (int i = 0; i < nDomains; i++)
	{
		if ((temp = getStatPtr(domainWindows[i].nCurrStats, domainWindows[i].currStats, VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON)) == NULL)
		{
			continue;
		}
		total += temp->val;
	}
	return total;
}

unsigned long long getDomainFreeTotal(int nDomains, virDomainWindow *domainWindows)
{
	unsigned long long total = 0;
	virDomainMemoryStatPtr temp = NULL;
	for (int i = 0; i < nDomains; i++)
	{
		if ((temp = getStatPtr(domainWindows[i].nCurrStats, domainWindows[i].currStats, VIR_DOMAIN_MEMORY_STAT_UNUSED)) == NULL)
		{
			continue;
		}
		total += temp->val;
	}
	return total;

}

//helper function - get the mem percentage for the particular statistic
int getUsagePercentage(int nParams, virDomainMemoryStatPtr stats)
{
	if (nParams == 0)
	{
		return 0;
	}
	unsigned long long balloonSize = getStatPtr(nParams, stats, VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON)->val;
	unsigned long long usedSize = balloonSize - getStatPtr(nParams, stats, VIR_DOMAIN_MEMORY_STAT_UNUSED)->val;
	int usagePercent = static_cast<int>((usedSize * 100.0)/balloonSize);
	//printf("Used size: %llu\nBalloon size: %llu\n", usedSize, balloonSize);
	//printf("Usage Percentage: %d\n", usagePercent);
	return usagePercent; 
}

//helper function - just print all values in in the virDomainMemoryStatPtr
void printStats(int nParams, virDomainMemoryStatPtr stats)
{
	printf("========== memory stats ==========\n");
	for (int i = 0; i < nParams; i++)
	{
		printf("%s: %llu\n", tagMap[stats[i].tag], stats[i].val);
	}
}

int getHostMemoryStats(virConnectPtr connection, hostMemoryStat *hostStats)
{
	int nParams = 0;
	virNodeMemoryStatsPtr params = NULL;
	//clear host stats before updating it
	hostStats->total = 0;
	hostStats->free = 0;
	if (virNodeGetMemoryStats(connection, VIR_NODE_MEMORY_STATS_ALL_CELLS, NULL, &nParams, 0) == 0 && nParams != 0)
	{
		if ((params = (virNodeMemoryStatsPtr)calloc(nParams, sizeof(virNodeMemoryStats))) == NULL)
		{
			printf("Error allocating parameters to get host memory stats.\n");
			return -1;
		}
		if ((virNodeGetMemoryStats(connection, VIR_NODE_MEMORY_STATS_ALL_CELLS, params, &nParams, 0)) < 0)
		{
			printf("Error getting host memory stats.\n");
			return -1;
		}
	}

	for (int i = 0; i < nParams; i++)
	{
		if (strcmp(params[i].field, VIR_NODE_MEMORY_STATS_TOTAL) == 0)
		{
			hostStats->total += params[i].value;
		}
		else if (strcmp(params[i].field, VIR_NODE_MEMORY_STATS_FREE) == 0)
		{
			hostStats->free += params[i].value;
		}
		else if (strcmp(params[i].field, VIR_NODE_MEMORY_STATS_BUFFERS) == 0)
		{
			hostStats->free += params[i].value;
		}
		else if (strcmp(params[i].field, VIR_NODE_MEMORY_STATS_CACHED) == 0)
		{
			hostStats->free += params[i].value;
		}
	}

	return 0;
}

//int freeResourcesForHost(int nDomains

//this is where logic for the memory policy is
int adjustResources (virConnectPtr connection, int nDomains, virDomainWindow *domainWindows)
{
	//stats of all domains
	unsigned long long domainBalloonTotal = getDomainBalloonTotal(nDomains, domainWindows);
	unsigned long long domainFreeTotal = getDomainFreeTotal(nDomains, domainWindows);
	unsigned long long domainUsageTotal = domainBalloonTotal - domainFreeTotal;
	printf("======================================\n"
		"Domain Totals:\n"
		"======================================\n"
		"Ballon Size: %llu\n"
		"Unused Size: %llu\n"
		"Used Size: %llu\n"
		"======================================\n",
		domainBalloonTotal, domainFreeTotal, domainUsageTotal);

	//host stats
	hostMemoryStat hostMemStat;
	getHostMemoryStats(connection, &hostMemStat);
	unsigned long long totalPhysicalMemory = hostMemStat.total;
	unsigned long long totalPhysicalUnusedMemory = hostMemStat.free;
	unsigned long long totalPhysicalUsedMemory = totalPhysicalMemory - totalPhysicalUnusedMemory;
	unsigned long long totalNonDomainMemory = totalPhysicalMemory - domainBalloonTotal;

	//calculate host specific usage stats
	unsigned long long totalHostUsage = totalPhysicalUsedMemory - domainUsageTotal;
	//this percentage represents the percentage of the non-domain memory that is used by the host
	int hostUsagePercentage = static_cast<int>((totalHostUsage * 100.0) / totalNonDomainMemory);
	printf("======================================\n"
		"Host Totals:\n"
		"======================================\n"
		"Physical Memory Size: %llu\n"
		"Unused: %llu\n"
		"Used: %llu\n"
		"Non-Domain Memory Size: %llu\n"
		"Host Specific Usage Percent: %d%%\n"
		"======================================\n",
		totalPhysicalMemory, totalPhysicalUnusedMemory, totalPhysicalUsedMemory,
		totalNonDomainMemory, hostUsagePercentage);

	//if host memory usage (host + vm usage combined) is in the hot zone, adjust VM memory
	//adjust all domains as soon as the host mem usage gets into the hot zone
	if (hostUsagePercentage >= percentThreshold[HOT])
	{
		//half the memory balloon of all VMs
		for (int i = 0; i < nDomains; i++)
		{
			unsigned long long balloonSize = getStatPtr(domainWindows[i].nCurrStats, domainWindows[i].currStats, VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON)->val;
			if ((virDomainSetMemory(domainWindows[i].domain, balloonSize >> 1)) < 0)
			{
				printf("Error halfing the size of the domain memory.\n");
			}
		}
	}
	
	//otherwise, adjust VM memory:
	//Go through each domain and double or half the memory size if needed 
	for (int i = 0; i < nDomains; i++)
	{
		int currPercentUsage = getUsagePercentage(domainWindows[i].nCurrStats, domainWindows[i].currStats);
		int prevPercentUsage = getUsagePercentage(domainWindows[i].nPrevStats, domainWindows[i].prevStats);
		unsigned long long balloonSize = getStatPtr(domainWindows[i].nCurrStats, domainWindows[i].currStats, VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON)->val;
		//only increase memory when two intervals of HOT memory usage occurs
		if (currPercentUsage >= percentThreshold[HOT] && prevPercentUsage >= percentThreshold[HOT])
		{
			printf("%s: %llu -> %llu\n", virDomainGetName(domainWindows[i].domain), balloonSize, balloonSize << 1);
			if ((virDomainSetMemory(domainWindows[i].domain, balloonSize << 1)) < 0)
			{
				printf("Error doubling the size of the domain memory.\n");
			}
		}
		//only decrease memory when two intervals of COLDL memory usage occurs
		else if (currPercentUsage <= percentThreshold[COLD] && prevPercentUsage <= percentThreshold[COLD])
		{
			printf("%s: %llu -> %llu\n", virDomainGetName(domainWindows[i].domain), balloonSize, balloonSize >> 1);
			if((virDomainSetMemory(domainWindows[i].domain, balloonSize >> 1)) < 0)
			{
				printf("Error halfing the size of the domain memory.\n");
			}
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	if (argc != 2)
	{
		printf("wrong number of arguments...aborting.\n");
		return -1;
	}

	int period = atoi(argv[1]);

	printf("period: %d\n", period);
	unsigned int flags = VIR_CONNECT_LIST_DOMAINS_RUNNING |
				VIR_CONNECT_LIST_DOMAINS_PERSISTENT;

	//host specific variables
	virConnectPtr conn;
	
	//domain specific variables
	int nDomains = 0;
	virDomainWindow *domainWindows = NULL;

	//open connection to the hypervisor
	conn = virConnectOpen(HYPERV_URI);
	if (conn == NULL)
	{
		printf("Error connecting to Hypervisor!\n");
		return -1;
	}
	
	if((nDomains = initDomainWindows(conn, domainWindows)) < 0)
	{
		printf("Error initializing domain windows. Aborting.\n");
		virConnectClose(conn);
		return -1;
	}

	if (nDomains == 0)
	{
		printf("No domains to coordinate. Aborting\n");
		virConnectClose(conn);
		return 0;
	}
	//set the statistics gathering interval for each domain
	for (int i = 0; i < nDomains; i++)
	{
		virDomainSetMemoryStatsPeriod(domains[i], period, VIR_DOMAIN_AFFECT_LIVE);
	}

	while (1)
	{
		//sleep first so that we can gather statistics after setting the period.
		sleep(period);
		if ((fetchStats(nDomains, domainWindows)) < 0)
		{
			printf("Error fetching domain memory stats.\n");
			virConnectClose(conn);
			return -1;
		}
		for (int i = 0; i < nDomains; i++)
		{
			printf("==================================\n"
				"Domain: %s\n", virDomainGetName(domainWindows[i].domain));
			printStats(domainWindows[i].nCurrStats, domainWindows[i].currStats);
		}
		if ((adjustResources(conn, nDomains, domainWindows)) < 0)
		{
			printf("Error adjusting domain memory resources. Aborting.\n");
			virConnectClose(conn);
		}
	}
	virConnectClose(conn);

	return 0;
}
