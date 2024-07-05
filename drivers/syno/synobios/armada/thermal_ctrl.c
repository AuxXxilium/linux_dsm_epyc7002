// Copyright (c) 2013 Synology Inc. All rights reserved.

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/interrupt.h>

#include <linux/cpuidle.h>
#include <linux/proc_fs.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/delay.h>

#ifdef OUTPUT_DBG
#define	DBG_MSG(fmt, arg...)	printk("%s.%s.%d"fmt, __FILE__, __FUNCTION__, __LINE__, ##arg)
#else
#define	DBG_MSG(fmt, arg...)
#endif

#define	THERMAL_STAGES			3
#define	DISABLE_IDLE_TIME_MS	1000
#define	MIN_IDLE_TIME_MS		5
#define	PRIORITY				99
#define	CHECK_PERIOD_MS			2000
#define THERMAL_NODE			"syno_thermal"

#define	GET_CPU_TEMP()	(axptemp_read_temp()-13)

#define	GET_ARRAY_ENTRY(ARRAY) (sizeof(ARRAY)/sizeof(ARRAY[0]))
#define	GET_NS_TIME(_CPU_TIME_)	do_div(_CPU_TIME_, 1000000000)
#define	NS_TO_MS_TIME(_NS_TIME_)  do_div(_NS_TIME_,1000)

/******************************************************
 * External APIs from Kernel
 *****************************************************/
extern int axptemp_read_temp(void);
extern unsigned long long force_cpu_idle(void);
extern unsigned long long get_cpu_time(void);
extern int set_schedule(int policy,const struct sched_param *param);

typedef enum {
	MODE_DISABLE = 0,
	MODE_ENABLE,

	MODE_END
} SYNO_THERMAL_Mode;

typedef struct {
	int StartTemp;
	int StopTemp;
	int TaskIdleTime;
} SYNO_STAGE_INFO;

typedef	struct {	
	struct proc_dir_entry* pProcFSEntry;
	struct task_struct * pThremalThread;
	struct task_struct * pThermalCtrlThread;
	SYNO_STAGE_INFO ThermalTable[THERMAL_STAGES];
	SYNO_THERMAL_Mode Mode;
	unsigned long long LogCPUIdleTime;
	unsigned long long LogThermalStartTime;
	unsigned long long LogThermalStopTime;
	unsigned long ThermalIdleJS;
	int Stage;
	int CheckTempPeriodMS; // MS
	unsigned int CPUTempTrend;
} SYNO_THREMAL_INFO;

static 
const SYNO_STAGE_INFO g_DftThermalTable[THERMAL_STAGES] = 
{
	{0, 0, DISABLE_IDLE_TIME_MS},
	{87, 84, 50},
	{89, 87, 10}
};

static 
const char *g_ModeString[MODE_END] = {"Disable" , "Enable"};

static 
SYNO_THREMAL_INFO g_SynoInfo;

static 
void SetStageArgs(SYNO_THREMAL_INFO* pInfo, const char* pArg, const char* pStageString, int Index)
{
	int arg_len;
	char* pend;

	SYNO_STAGE_INFO Entry = g_DftThermalTable[Index];

	pArg = strstr(pArg, pStageString);
	if (pArg) {
		DBG_MSG("pArg=%s, atoi=%s\n", pArg, &pArg[arg_len]);
		arg_len = strlen(pStageString);
		Entry.StartTemp = simple_strtol(&pArg[arg_len], &pend, 0); 		
		if (pend) {
			pArg = strchr(pend, ',');
			if (pArg) {
				DBG_MSG("%s.%d: pend=%s, atoi=%s\n", pend, &pArg[1]);
				Entry.StopTemp = simple_strtol(&pArg[1], &pend, 0); // ignore 
			}
			if (pend) {
				pArg = strchr(pend, ',');
				if (pArg) {
					DBG_MSG("pend=%s, pArg=%s\n", pend, &pArg[1]);
					Entry.TaskIdleTime = simple_strtol(&pArg[1], &pend, 0); // ignore 
				}
			}
		}
	}
	
	if (Entry.TaskIdleTime < MIN_IDLE_TIME_MS) {
		Entry.TaskIdleTime = MIN_IDLE_TIME_MS;
	}

	DBG_MSG("ThermalStartTemp=%d, ThermalStopTemp=%d, idle_time=%d\n", Entry.StartTemp, Entry.StopTemp, Entry.TaskIdleTime);
	pInfo->ThermalTable[Index] = Entry;
}


static 
int SynoThermalWrite(struct file *pFile, const char *pBuffer,
			     unsigned long Count, void *pData)
{
	if (!strncmp (pBuffer, "enable", strlen("enable"))) {
		preempt_disable();
		SetStageArgs(&g_SynoInfo, pBuffer, "s1:", 1);
		SetStageArgs(&g_SynoInfo, pBuffer, "s2:", 2);
		g_SynoInfo.LogCPUIdleTime = 0;
		g_SynoInfo.Stage = 0;
		preempt_enable();
		g_SynoInfo.LogThermalStartTime = get_cpu_time();
		barrier();
		g_SynoInfo.Mode = MODE_ENABLE;
	} else if (!strncmp (pBuffer, "disable", strlen("disable"))) {
		g_SynoInfo.Mode = MODE_DISABLE;
		g_SynoInfo.LogThermalStopTime = get_cpu_time();
	}

	return Count;
}

static 
int ShowUsage(char *pBuffer, int Len)
{
	int i;
	Len += sprintf(&pBuffer[Len], "Usage:\n\t enable,[s1:start_tmp,stop_tmp,idle_time s2:start_tmp,stop_tmp,idle_time]: start thermal task...\n");
	Len += sprintf(&pBuffer[Len], "\t disable: stop thermal task\n");
	Len += sprintf(&pBuffer[Len], "\t Argaments within [] is optional\n");
	Len += sprintf(&pBuffer[Len], "\t Default cmd = enable,");

	for (i = 1; i < THERMAL_STAGES; i++) {
		Len += sprintf(&pBuffer[Len], "s%d:%d,%d,%d ", i,
									g_DftThermalTable[i].StartTemp, 
									g_DftThermalTable[i].StopTemp, 
									g_DftThermalTable[i].TaskIdleTime);
	}
	Len += sprintf(&pBuffer[Len], "\n\t Example: echo enable > /proc/%s\n", THERMAL_NODE);
	Len += sprintf(&pBuffer[Len], "\t Example: echo enable,s1:80,75,50 s2:85,82,10 > /proc/%s\n", THERMAL_NODE);
	return Len;		
}

static 
int ShowInfo(char *pBuffer, int Len, unsigned long long TotalTime)
{
	unsigned long long IdleTime;
	unsigned long IdleTimeNS, TotalTimeNS;
	unsigned long Percentage = 0;
	int i, Stage, CPUTrendNum;
	unsigned int CPUTempTrend;
	char Buffer[64];

	IdleTime = g_SynoInfo.LogCPUIdleTime;
	Stage = g_SynoInfo.Stage;

	IdleTimeNS = GET_NS_TIME(IdleTime);
	TotalTimeNS = GET_NS_TIME(TotalTime);

	NS_TO_MS_TIME(IdleTimeNS);
	NS_TO_MS_TIME(TotalTimeNS);

	if (TotalTime) {
		Percentage = (unsigned long)(IdleTime*100);
		/* be careful, first argument of do_div will be modified */
		do_div(Percentage, (unsigned long)TotalTime);
	}

	CPUTrendNum = sizeof(CPUTempTrend)*8;
	CPUTempTrend = g_SynoInfo.CPUTempTrend;
	for (i = 0; i < CPUTrendNum; i++) {
		Buffer[i] = (char)(((CPUTempTrend >> i)& 0x01)|0x30);
	}
	Buffer[CPUTrendNum]='\0';
	
	Len += sprintf(&pBuffer[Len],	"Status:\n\t Mode: [%s]\n"
									"\t Current stage[%d]: start temp: %2d, stop temp: %2d, idle time: %2d ms\n",
							 		g_ModeString[g_SynoInfo.Mode], Stage, 
							 		g_SynoInfo.ThermalTable[Stage].StartTemp, 
							 		g_SynoInfo.ThermalTable[Stage].StopTemp,  
							 		g_SynoInfo.ThermalTable[Stage].TaskIdleTime); 
	
	for (i = 1 ; i < THERMAL_STAGES; i++ ) {
		Len += 	sprintf(&pBuffer[Len],	"\t Stage[%d]: start temp: %2d, stop temp: %2d, idle time: %2d ms\n", i, 										
									g_SynoInfo.ThermalTable[i].StartTemp, 
									g_SynoInfo.ThermalTable[i].StopTemp,  
									g_SynoInfo.ThermalTable[i].TaskIdleTime); 
										
	}
	
	Len += sprintf(&pBuffer[Len],	"\t CPU temp trend: %s [1: cpu temp was rising compared to last sample]\n"
									"\t Idle time  [%5lu.%06lu] \n"
									"\t Total time [%5lu.%06lu] \n"
									"\t Idle percentage: %2lu %%\n",
							 		Buffer,
							 		(unsigned long)IdleTime, IdleTimeNS,
							 		(unsigned long)TotalTime, TotalTimeNS, 
							 		Percentage);

	Len += sprintf(&pBuffer[Len], "\t Current CPU temp = %d\n", GET_CPU_TEMP());
	
	return Len;
}

static 
int SynoThermalRead(char *pOutBuffer, char **pBufferLocation, off_t Offset,
			    int BufferLength, int *pZero, void *pObj)
{
	unsigned long long TotalTime;
	int Len = 0;
	
	if (Offset > 0)
		return 0;
	
	if (g_SynoInfo.Mode >= MODE_END) {
		DBG_MSG("Wrong Mode [%d]....\n", g_SynoInfo.Mode);
		return -1;
	}

	if (MODE_DISABLE == g_SynoInfo.Mode) {
		TotalTime = g_SynoInfo.LogThermalStopTime - g_SynoInfo.LogThermalStartTime;
	} else {
		TotalTime = get_cpu_time() - g_SynoInfo.LogThermalStartTime;
	}

	Len += ShowUsage(pOutBuffer, Len);
	
	return ShowInfo(pOutBuffer, Len, TotalTime);
}


static
void CreateProc(struct proc_dir_entry** pOutEntry)
{
	struct proc_dir_entry* pEntry;

	pEntry = create_proc_entry(THERMAL_NODE, 0666, NULL);
	pEntry->read_proc = SynoThermalRead;
	pEntry->write_proc = SynoThermalWrite;
	pEntry->nlink = 1;
	*pOutEntry = pEntry;
}

static
void DeleteProc(struct proc_dir_entry** pEntry)
{
	if (pEntry && *pEntry) {
		remove_proc_entry(THERMAL_NODE, NULL);
		*pEntry = NULL;
	}
}

static
int SynoIdleThead(void* pObject) {
	struct sched_param param;

	param.sched_priority = PRIORITY;
	if (0 != set_schedule(SCHED_RR, &param)){
		printk("failed to set RR schedule\n");
	} 
	
	while (!kthread_should_stop()) {				
		if (g_SynoInfo.Stage <= 0) {
			msleep(DISABLE_IDLE_TIME_MS);
			continue;
		}

		g_SynoInfo.LogCPUIdleTime += force_cpu_idle();		
		schedule_timeout_uninterruptible(g_SynoInfo.ThermalIdleJS);
	}	
	
	return 0;
}

inline 
void StageChange(SYNO_THREMAL_INFO* pSynoInfo, int NewStage, int CPUTemp)
{
	if (NewStage != pSynoInfo->Stage) {
		pSynoInfo->ThermalIdleJS = msecs_to_jiffies(pSynoInfo->ThermalTable[NewStage].TaskIdleTime);	
		pSynoInfo->Stage = NewStage;
		DBG_MSG("Enter Stage: %d, CPU=%d\n", NewStage, CPUTemp);
	}
}

static
int SynoThermalCtrlThread(void* pObject)
{
	int CPUTemp, LastCPUTemp = 0;
	int NewStage;
	
	while (!kthread_should_stop()) {		
		if (g_SynoInfo.Mode == MODE_DISABLE) {
			g_SynoInfo.Stage = 0;
			msleep(DISABLE_IDLE_TIME_MS);
			continue;
		}
		
		CPUTemp = GET_CPU_TEMP();
		
		if (CPUTemp > LastCPUTemp) {
			g_SynoInfo.CPUTempTrend |= 1;
			
			NewStage = g_SynoInfo.Stage + 1;
			if (NewStage >= THERMAL_STAGES) {
				NewStage = THERMAL_STAGES - 1;
			}
			
			if (CPUTemp >= g_SynoInfo.ThermalTable[NewStage].StartTemp) {
				StageChange(&g_SynoInfo, NewStage, CPUTemp);
			}
		} else {
			NewStage = g_SynoInfo.Stage;			
			if (CPUTemp <= g_SynoInfo.ThermalTable[NewStage].StopTemp) {
				NewStage--;
				if (NewStage < 0) {
					NewStage = 0;
				}
				StageChange(&g_SynoInfo, NewStage, CPUTemp);
			}
		}

		LastCPUTemp = CPUTemp;
		g_SynoInfo.CPUTempTrend <<= 1;
		msleep(g_SynoInfo.CheckTempPeriodMS);
	}		

	return 0;
}

static 
int SynoThermalCtrlInit(void)
{
	memset(&g_SynoInfo, 0, sizeof(SYNO_THREMAL_INFO));
	memcpy(&g_SynoInfo.ThermalTable, g_DftThermalTable, sizeof(g_SynoInfo.ThermalTable));
	g_SynoInfo.CheckTempPeriodMS = CHECK_PERIOD_MS;
	/* set default mode to enable */
	g_SynoInfo.Mode = MODE_ENABLE;
	
	CreateProc(&g_SynoInfo.pProcFSEntry);	
	g_SynoInfo.pThremalThread = kthread_run(SynoIdleThead, NULL, "thermal");
	g_SynoInfo.pThermalCtrlThread = kthread_run(SynoThermalCtrlThread, NULL, "thermal_ctrl");

	return 0;
}
 
static 
void SynoThermalCtrlCleanup(void)
{
	DeleteProc(&g_SynoInfo.pProcFSEntry);

	if (g_SynoInfo.pThremalThread) {
		kthread_stop(g_SynoInfo.pThremalThread);
		g_SynoInfo.pThremalThread = NULL;
	}
	
	if (g_SynoInfo.pThermalCtrlThread) {
		kthread_stop(g_SynoInfo.pThermalCtrlThread);
		g_SynoInfo.pThermalCtrlThread = NULL;
	}
}
 
module_init(SynoThermalCtrlInit);
module_exit(SynoThermalCtrlCleanup);
 
MODULE_AUTHOR("King Huang");
MODULE_DESCRIPTION("syno thermal control module\n") ;
MODULE_LICENSE("Synology Inc.");

