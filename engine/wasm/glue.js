mergeInto(LibraryManager.library, {
    js_loaded: function () {
        if (Module["loaded"])
            Module["loaded"]();
    },
    js_send_progress: function (iteration, total_iterations,
        phase, total_phases,
        phase_name, subphase_name) {
        var progress = {
            "iteration": iteration,
            "totalIterations": total_iterations,
            "phase": phase,
            "totalPhases": total_phases,
            "phaseName": UTF8ToString(phase_name),
            "subphaseName": UTF8ToString(subphase_name)
        };
        if (Module["updateProgress"])
            Module["updateProgress"](progress);
    }

});