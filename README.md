# pidstat

mmm external/pidstat -j64

adb root

adb remount

adb push pidstat /system/bin

# pidstat 使用

pidstat主要用于监控全部或指定进程占用系统资源的情况，如CPU，内存、设备IO、任务切换、线程等。pidstat首次运行时显示自系统启动开始的各项统计信息，之后运行pidstat将显示自上次运行该命令以后的统计信息。用户可以通过指定统计的次数和时间来获得所需的统计信息。


常用参数：

```C
-C comm		#只显示那些包含字符串（可是正则表达式）comm的命令的名字
 -d			#显示I/O统计信息（须内核2.6.20及以后）
    PID			        #进程号
    kB_rd/s			#每秒此进程从磁盘读取的千字节数
    kB_wr/s			#此进程已经或者将要写入磁盘的每秒千字节数
    kB_ccwr/s			#由任务取消的写入磁盘的千字节数
    Command			#命令的名字
 -h			#显示所有的活动的任务
 -I			#在SMP环境，指出任务的CPU使用（等同于选项-u）应该被除于cpu的总数
 -l			#显示进程的命令名和它的参数
 -p { pid [,...] | SELF | ALL }		#指定线程显示其报告
 -r			#显示分页错误的内存利用率
    When reporting statistics for individual tasks, the following values are displayed:
    PID			        #进程号
    minflt/s			#每秒次缺页错误次数(minor page faults)，次缺页错误次数意即虚拟内存地址映射成物理内存地址产生的page fault次数
    majflt/s			#每秒主缺页错误次数(major page faults)，当虚拟内存地址映射成物理内存地址时，相应的page在swap中，这样的page fault为major page fault，一般在内存使用紧张时产生
    VSZ			        #该进程使用的虚拟内存(以kB为单位)
    RSS			        #该进程使用的物理内存(以kB为单位)
    %MEM			#当前任务使用的有效内存的百分比
    Command			#任务的命令名
    When reporting global statistics for tasks and all their children, the following values are displayed:
    PID			        #PID号
    minflt-nr			#在指定的时间间隔内收集的进程和其子进程的次缺页错误次数
    majflt-nr			#在指定的时间间隔内收集的进程和其子进程的主缺页错误次数
    Command			#命令名
 -s			#堆栈的使用
 -t			#显示与所选任务相关的线程的统计数据
 -T { TASK | CHILD | ALL }	#指定必须监测的内容：TASK是默认的，单个任务的报告；CHILD：指定的进程和他们的子进程的全局报告，ALL：相当于TASK和CHILD
 -u			#报告CPU使用
    When reporting statistics for individual tasks, the following values are displayed:
    PID
    %usr			#用户层任务正在使用的CPU百分比（with or without nice priority ，NOT include time spent running a virtual processor）
    %system			#系统层正在执行的任务的CPU使用百分比
    %guest			#运行虚拟机的CPU占用百分比
    %CPU			#所有的使用的CPU的时间百分比
    CPU			        #处理器数量
    Command			#命令
    When reporting global statistics for tasks and all their children, the following values are displayed:
    PID			        #PID号
    usr-ms			#在指定时间内收集的在用户层执行的进程和它的子进程占用的CPU时间（毫秒）{with or without nice priority，NOT include time spent running a virtual processor)
    system-ms			#在指定时间内收集的在系统层执行的进程和它的子进程占用的CPU时间（毫秒）
    guest-ms			#花在虚拟机上的时间
    Command			#命令
 -V			#版本号
 -w			#报告任务切换情况
    PID			        #PID号
    cswch/s			#每秒自动上下文切换
    nvcswch/s			#每秒非自愿的上下文切换
    Command			#命令

```
