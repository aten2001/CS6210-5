#include <libvirt/libvirt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HYPERV_URI 	"qemu:///system"
#define MAX_CPUS	128

struct CPU_STATS
{
	unsigned long long utilizationStats;
	unsigned long long idleStats;
};

int main()
{
	int nDomains= 0;
	unsigned int flags = VIR_CONNECT_LIST_DOMAINS_RUNNING |
				VIR_CONNECT_LIST_DOMAINS_PERSISTENT;
	virConnectPtr conn;
	virDomainPtr *domains;
	virVcpuInfoPtr vcpuInfo;
	//virNodeCPUStatsPtr *cpuStatsTable = NULL;
	CPU_STATS cpuStats[MAX_CPUS] = {0};
	virNodeCPUStatsPtr cpuStatsParams = NULL;
	conn = virConnectOpen(HYPERV_URI);
	if (conn == NULL)
	{
		printf("Error connecting to Hypervisor!\n");
	}
	
	nDomains = virConnectListAllDomains(conn, &domains, flags);
	if (nDomains < 0)
	{
		printf("Error getting domains list.\n");
		virConnectClose(conn);
		//error();
	}
	
	printf("Number of domains: %d\n", nDomains);
	int numCpus = virDomainGetCPUStats(domains[0], NULL, 0, 0, 0, 0);
	printf("num cpus: %d\n", numCpus);
	printf("vir cpu maplen: %d\n", VIR_CPU_MAPLEN(numCpus));


	//allocate cpu stats table
	//cpuStatsTable = (virNodeCPUStatsPtr*)calloc(numCpus, sizeof(*cpuStatsTable));

	//get stats of each CPU
	memset(cpuStats, 0, numCpus * sizeof(CPU_STATS));
	for (int i = 0; i < numCpus; i++)
	{
		//get the number of params available for the cpu stat
		int npcpuStatsParams = 0;
		if (virNodeGetCPUStats(conn, i, NULL, &npcpuStatsParams, 0) == 0 && npcpuStatsParams != 0)
		{
			printf("npcpuStatsParams%d: %d\n", i, npcpuStatsParams);
			cpuStatsParams = (virNodeCPUStatsPtr)calloc(npcpuStatsParams, sizeof(*cpuStatsParams));
		}

		//parse each param
		virNodeGetCPUStats(conn, i, cpuStatsParams, &npcpuStatsParams, 0);
		for (int j = 0; j < npcpuStatsParams; j++)
		{
			printf("Field%d: %s\n", j, cpuStatsParams[j].field);
			printf("Value: %llu\n", cpuStatsParams[j].value);
			if (strcmp(VIR_NODE_CPU_STATS_IDLE, cpuStatsParams[j].field) == 0 ||
				strcmp(VIR_NODE_CPU_STATS_IOWAIT, cpuStatsParams[j].field) == 0)
			{
				cpuStats[i].idleStats += cpuStatsParams[j].value;
			}
			else if (strcmp(VIR_NODE_CPU_STATS_KERNEL, cpuStatsParams[j].field) == 0 ||
				strcmp(VIR_NODE_CPU_STATS_USER, cpuStatsParams[j].field) == 0)
			{
				cpuStats[i].utilizationStats += cpuStatsParams[j].value;
			}
				 
		}
		free(cpuStatsParams);
		cpuStatsParams = NULL;
	}

	for (int i = 0; i < numCpus; i++)
	{
		printf("pcpu%d stats:\n", i);
		printf("utilization: %llu\nidle: %llu\n=============\n\n", cpuStats[i].utilizationStats, cpuStats[i].idleStats);
		printf("utilization percentage: %.2f\n-------------\n\n", (cpuStats[i].utilizationStats * 100.0)/(cpuStats[i].utilizationStats + cpuStats[i].idleStats));
	}

	for (int i = 0; i < nDomains; i++)
	{
		int vcpus = virDomainGetVcpusFlags(domains[i], VIR_DOMAIN_VCPU_CURRENT);
		vcpuInfo = (virVcpuInfoPtr)calloc(vcpus, sizeof(virVcpuInfo));
		printf("Number of vcpus: %d\n", vcpus);
		//if (vcpus < 0)
		
		virDomainGetVcpus(domains[i], vcpuInfo, vcpus, NULL, 0);
		for (int i = 0; i < vcpus; i++)
		{
			printf("vcpu %u:\nstate:%d\ncpuTime:%llu\ncpu:%d\n=========\n",
				vcpuInfo[i].number, vcpuInfo[i].state, vcpuInfo[i].cpuTime, vcpuInfo[i].cpu);
		}
		//virDomainGetVcpus(
		free(vcpuInfo);
		vcpuInfo = NULL;

	} 

	if (nDomains > 0)
	{
		printf("printing cpu stats\n");
		int num_cpus = virDomainGetCPUStats(domains[0], NULL, 0, 0, 0, 0);
		if (num_cpus < 0)
		{
			printf("Error: num_cpus errored out\n");
			return 1;
		}
		int nparams = virDomainGetCPUStats(domains[0], NULL, 0, 0, 1, 0);
		if (nparams < 0)
		{
			printf("Error, nparams errored out\n");
			return 1;
		}

		printf("num_cpus: %d\nnparams: %d\n", num_cpus, nparams);
		
		virTypedParameterPtr params = NULL; 
		params = (virTypedParameterPtr)calloc(nparams * num_cpus, sizeof(*params));
		if (params == NULL)
		{
			return 1;
		} 
		if(virDomainGetCPUStats(domains[0], params, nparams, 0, num_cpus, 0)<0)
		{
			printf("Error getting CPU stats.\n");
			return 1;
		}

		for (int i = 0; i < num_cpus; i++)
		{
			printf("pcpu%d stats: \n", i);
			for (int j = 0; j < nparams; j++)
			{
				if (strcmp(params[i*nparams+j].field, VIR_DOMAIN_CPU_STATS_CPUTIME) == 0)
				{
					printf ("cpu_time: %llu\n", params[i].value.ul);
				}
				else if (strcmp(params[i*nparams+j].field, VIR_DOMAIN_CPU_STATS_VCPUTIME) == 0)
				{
					printf("vcpu_time: %llu\n", params[i].value.ul);
				}
			}
		}

		free(params);
	}
	virConnectClose(conn);
	
	for (int i = 0; i < nDomains; i++)
	{
		virDomainFree(domains[i]);
	}
}
