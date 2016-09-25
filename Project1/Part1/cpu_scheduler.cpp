#include <libvirt/libvirt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define HYPERV_URI 	"qemu:///system"
#define MAX_CPUS	128
#define MAX_MAPPING_BYTES (MAX_CPUS >> 3)

static virDomainPtr *domains = NULL;

//tracks the load for each PCPU
static unsigned long long cpuTimes[MAX_CPUS] = {0};

struct virDomainWindow
{
	virDomainPtr domain;
	unsigned short nVcpus;
	//nPcpus and nParams used by virDomainGetCPUStats only.
	//not needed for my implementation but will keep it there just in case.
	int nPcpus;
	int nParams;
	virVcpuInfoPtr prevStats;
	virVcpuInfoPtr currStats;
	//cpumaps isn't really used, but I'll just keep it here
	unsigned char *cpumaps;
	virDomainWindow() :
		domain(NULL),
		nVcpus(0),
		nPcpus(0),
		nParams(0),
		prevStats(NULL),
		currStats(NULL),
		cpumaps(NULL)
	{}
};

void destroyDomainWindows(int nDomains, virDomainWindow *domainWindows)
{
	printf("Destroying domain windows\n");
	for (int i = 0; i < nDomains; i++)
	{
		virDomainFree(domainWindows[i].domain);
		if (domainWindows[i].prevStats)
		{
			free(domainWindows[i].prevStats);
		}
		if (domainWindows[i].currStats)
		{
			free(domainWindows[i].currStats);
		}
		if (domainWindows[i].cpumaps)
		{
			free(domainWindows[i].cpumaps);
		}
	}
}
//return number of domains and fill output parameter with all domain windows
int initDomainWindows(virConnectPtr connection, virDomainWindow *&domainWindows)
{
	unsigned int flags = VIR_CONNECT_LIST_DOMAINS_RUNNING | VIR_CONNECT_LIST_DOMAINS_PERSISTENT;
	int nDomains = 0;
	virDomainInfo domainInfo;
	
	if((nDomains = virConnectListAllDomains(connection, &domains, flags)) < 0)
	{
		printf("Error getting domains list.\n");
	}

	domainWindows = (virDomainWindow*)calloc(nDomains, sizeof(virDomainWindow));
	printf("Number of domains: %d\n", nDomains);

	for (int i = 0; i < nDomains; i++)
	{
		domainWindows[i].domain = domains[i];
		//get number of vcpus in the domain
		if ((domainWindows[i].nVcpus = virDomainGetVcpusFlags(domainWindows[i].domain, VIR_DOMAIN_VCPU_CURRENT)) < 0)
		{
			printf("Error getting number of vCpus for domain %d\n", i);
			return -1;
		}	
		//nParams and nPcpus is not used, but will keep it here for now.
		//get the number of params available for each pcpu
		if ((domainWindows[i].nParams = virDomainGetCPUStats(domainWindows[i].domain, NULL, 0, 0, 1, 0)) < 0)
		{
			printf("Error getting number of params for each vCpu.\n");
			return -1;
		}
		//get number of pcpus this domain can map to
		if ((domainWindows[i].nPcpus = virDomainGetCPUStats(domainWindows[i].domain, NULL, 0, 0, 0, 0)) < 0)
		{
			printf("Error getting number of pCpus for domain %d\n", i);
			return -1;
		}
		
		//allocate both previous and current stats arrays
		domainWindows[i].prevStats = (virVcpuInfoPtr)calloc(domainWindows[i].nVcpus, sizeof(virVcpuInfo));
		domainWindows[i].currStats = (virVcpuInfoPtr)calloc(domainWindows[i].nVcpus, sizeof(virVcpuInfo));

		domainWindows[i].cpumaps = (unsigned char*)calloc(domainWindows[i].nVcpus, VIR_CPU_MAPLEN(domainWindows[i].nPcpus));
		printf("domain %s info:\n", virDomainGetName(domainWindows[i].domain));
		printf("\tnVcpu:%d\n....................\n", domainWindows[i].nVcpus);
	}

	return nDomains;
}

int fetchStats(int nDomains, virDomainWindow *domainWindows)
{
	printf("Fetching Vcpu stats...");
	for (int i = 0; i < nDomains; i++)
	{
		//copy last stats into previous to so that we can get time interval
		memcpy(domainWindows[i].prevStats, domainWindows[i].currStats, domainWindows[i].nVcpus*sizeof(virVcpuInfo));
		if ((virDomainGetVcpus(domainWindows[i].domain, domainWindows[i].currStats, domainWindows[i].nVcpus, domainWindows[i].cpumaps, VIR_CPU_MAPLEN(domainWindows[i].nPcpus))) < 0)
		{
			printf("\nError getting VCPU stats. Aborting.\n");
			return -1;
		}
	}
	printf("done.\n");

	return 0;
}

int getNextPcpuIndex(unsigned long long *cpuTimes, int max, int startIndex=0)
{
	printf("max: %d\n", max);
	int pcpuPin = startIndex%max;
	printf("startIndex: %d\n", pcpuPin);
	
	for (int i = 0; i < max; i++)
	{
		//just return the index i if value here is 0
		if (cpuTimes[(i+startIndex)%max] == 0)
		{
			printf("new cpu\n");
			return (i+startIndex)%max;
		}
		
		pcpuPin = cpuTimes[(i+startIndex)%max] < cpuTimes[pcpuPin] ? (i+startIndex)%max : pcpuPin;
	}
	return pcpuPin;
}

void printCpuMapping(int nDomains, virDomainWindow *domainWindows)
{
	virVcpuInfoPtr infoPtr = NULL; 

	for (int i = 0; i < nDomains; i++)
	{
		infoPtr = (virVcpuInfoPtr)calloc(domainWindows[i].nVcpus, sizeof(virVcpuInfo));
		virDomainGetVcpus(domainWindows[i].domain, infoPtr, domainWindows[i].nVcpus, NULL, 0);
		printf("\n\nDomain %s mapping:\n", virDomainGetName(domainWindows[i].domain));
		for (int j = 0; j < domainWindows[i].nVcpus; j++)
		{
			printf("vcpu%d\t", infoPtr[j].number);
		}
		printf("\n---------------------------\n");
		for (int j = 0; j < domainWindows[i].nVcpus; j++)
		{
			printf("%d\t", infoPtr[j].cpu);
		}
		printf("\n\n");
		free(infoPtr);
	}
}

void setNewPinMappings(int nDomains, virDomainWindow *domainWindows)
{
	printf("=============================================\n");
	printf("\n\nSetting new pin mappings\n");
	printf("\n-------------------------------------------\n");
	
	//copy cpu usage table to represent expected workload
	unsigned long long expectedWorkload[MAX_CPUS];
	memcpy(expectedWorkload, cpuTimes, MAX_CPUS);
	//pin mapping bytes
	unsigned char mappings[MAX_MAPPING_BYTES];
	int pin = 0;
	int maxNPcpus = 0;

	//go through each vcpu and map the pins
	for (int i = 0; i < nDomains; i++)
	{
		printf("Domain: %s\n", virDomainGetName(domainWindows[i].domain));
		maxNPcpus = domainWindows[i].nPcpus > maxNPcpus ? domainWindows[i].nPcpus : maxNPcpus;
		for (int j = 0; j < domainWindows[i].nVcpus; j++)
		{
			printf("Vcpu%d:\n", domainWindows[i].currStats[j].number);
			//clear mappings
			memset(mappings, 0, VIR_CPU_MAPLEN(maxNPcpus));
			//skip pin mapping if there is no cpu usage difference
			unsigned long long diff = domainWindows[i].currStats[j].cpuTime - domainWindows[i].prevStats[j].cpuTime;
			if (diff == 0)
			{
				continue;
			}
			//get next cpu pin, start at current pin setting. If it is unmapped during
			//this round, keep it at this pin setting to avoid having to switch unneccessarily.
			pin = getNextPcpuIndex(expectedWorkload, domainWindows[i].nPcpus, domainWindows[i].currStats[j].cpu);
			expectedWorkload[pin] += diff;
			cpuTimes[domainWindows[i].currStats[j].cpu] += diff;
			//if calculated pin is the same as last pin mapped, skip the pin mapping
			if (pin == domainWindows[i].currStats[j].cpu)
			{
				continue;
			}
			printf("pin mapping: vcpu%d -> pcpu%d\n", domainWindows[i].currStats[j].number, pin);
			mappings[pin/8] = 1 << (pin % 8);
			if(virDomainPinVcpu(domainWindows[i].domain, domainWindows[i].currStats[j].number, mappings, VIR_CPU_MAPLEN(domainWindows[i].nPcpus)) < 0)
			{
				printf("Warning! Mapping did not succeed!\n");
			}
			printf("maplen: %d\n", VIR_CPU_MAPLEN(domainWindows[i].nPcpus));
		}
		printf("\n-------------------------------------------\n");
	}
//	//update cpu usage table first
//	for (int i = 0; i < nDomains; i++)
//	{
//		for(int j = 0; j < domainWindows[i].nVcpus; j++)
//		{
//			unsigned long long diff = domainWindows[i].currStats[j].cpuTime - domainWindows[i].prevStats[j].cpuTime;
//		}
//	}
}

int main(int argc, char **argv)
{
	printf("max ull: %zu\n", sizeof(unsigned long long));
	if (argc != 2)
	{
		printf("wrong number of arguments...aborting.\n");
		return -1;
	}

	int sleepTime = atoi(argv[1]);

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
	}
	
	if((nDomains = initDomainWindows(conn, domainWindows)) < 0)
	{
		printf("Error initializing domain windows. Aborting.\n");
		virConnectClose(conn);
		return -1;
	}

	//initializes the beginning stats
	if ((fetchStats(nDomains, domainWindows)) < 0)
	{
		printf("Error fetching VCPU stats. Aborting.\n");
		destroyDomainWindows(nDomains, domainWindows);
		virConnectClose(conn);
		return -1;
	}

	while(1)
	{
		sleep(sleepTime);
		printCpuMapping(nDomains, domainWindows);

		if ((fetchStats(nDomains, domainWindows)) < 0)
		{
			printf("Error fetching VCPU stats. Aborting.\n");
			destroyDomainWindows(nDomains, domainWindows);
			virConnectClose(conn);
			return -1;
		}
		
		setNewPinMappings(nDomains, domainWindows);
		//print cpu usage table
		printf("CPU Usage:\n");
		for (int i = 0; i < domainWindows[0].nPcpus; i++)
		{
			printf("pcpu%d: %llu\n", i, cpuTimes[i]);
		}
	}

	destroyDomainWindows(nDomains, domainWindows);
	virConnectClose(conn);
	return 0;
}
