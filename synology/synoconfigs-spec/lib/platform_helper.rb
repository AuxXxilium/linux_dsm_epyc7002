
class Platform
    module Helper
        def x86_64?
            @family == :x86_64
        end

        def aarch64?
            @family == :armv8
        end

        def virtual?
            @target == :KVMX64 || @target == :KVMX64SOFS || @target == :KVMX64V2
        end

        def numa?
            @target == :PURLEY || @target == :EPYC7002 || @target == :EPYC7002SOFS || @target == :EPYC7003NTB
        end
    end
end
