
require 'syno_kconfig'

describe 'Debug options' do
    include SynoKconfig

    %w[
	CONFIG_TRACEPOINTS

	CONFIG_DEBUG_INFO
	CONFIG_DEBUG_INFO_BTF

	CONFIG_SCHED_DEBUG
	CONFIG_SLUB_DEBUG
    ].each do |cfg|
        it "#{cfg}=y" do
            platforms.verify(cfg, builtin?)
        end
    end

    %w[
        CONFIG_DEBUG_ATOMIC_SLEEP
        CONFIG_PREEMPT_COUNT
    ].each do |cfg|
        it "#{cfg} is enabled for X86_64 only" do
            platforms
                .select { |p| p.x86_64? }
                .verify(cfg, builtin?)

            platforms
                .reject { |p| p.x86_64? }
                .verify(cfg, disabled?)
        end
    end
end

