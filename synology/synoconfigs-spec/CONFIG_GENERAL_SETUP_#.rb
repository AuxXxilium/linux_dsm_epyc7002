
require 'syno_kconfig'

describe 'General setup' do
    include SynoKconfig

    #
    # built-in configs for all platforms
    #
    %w[
	CONFIG_SWAP
	CONFIG_SYSVIPC
	CONFIG_SYSVIPC_SYSCTL
	CONFIG_POSIX_MQUEUE
	CONFIG_POSIX_MQUEUE_SYSCTL
	CONFIG_USELIB
	CONFIG_AUDIT

	CONFIG_PREEMPT_NONE

	CONFIG_TASKSTATS
	CONFIG_TASK_DELAY_ACCT
	CONFIG_TASK_XACCT
	CONFIG_TASK_IO_ACCOUNTING
	CONFIG_PSI

	CONFIG_VM_EVENT_COUNTERS
	CONFIG_SLUB

	CONFIG_TRACEPOINTS

	CONFIG_RD_LZMA
    ].each do |cfg|
        it "#{cfg}=y" do
            platforms.verify(cfg, builtin?)
        end
    end

    #
    # disabled configs for all platforms
    #
    %w[
	CONFIG_PREEMPT_VOLUNTARY
	CONFIG_PREEMPT

	CONFIG_PSI_DEFAULT_DISABLED
    ].each do |cfg|
        it "#{cfg} is not set" do
            platforms.verify(cfg, disabled?)
        end
    end

    it "CONFIG_PHYSICAL_START=0x200000 for x86_64" do
        platforms
            .select { |p| p.family == :x86_64 }
            .verify('CONFIG_PHYSICAL_START', equaled?(0x200000))
        platforms
            .select { |p| p.family != :x86_64 }
            .verify('CONFIG_PHYSICAL_START', disabled?)
    end
end

