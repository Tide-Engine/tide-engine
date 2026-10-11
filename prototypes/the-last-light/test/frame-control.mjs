// Serialized into the browser fixture. The runner advances both players one
// frame at a time; time spent compiling, rendering or capturing is not match
// time. Real-time session deadlines belong to the session/rooms tests.
export function installFrameControl(scope) {
    const frames = new Map(), queues = new Set(), contexts = new Set();
    const wallNow = scope.performance.now.bind(scope.performance);
    let now = 0, next = 0, stepping = false;
    let submitted = 0, completed = 0, longest = 0, pendingAt = null;
    Object.defineProperty(scope.performance, 'now', { value: () => now });
    scope.__lastLightWallNow = wallNow;
    scope.requestAnimationFrame = callback => {
        if (typeof callback !== 'function') throw new TypeError('Animation frame callback must be a function');
        const id = ++next;
        frames.set(id, callback);
        return id;
    };
    scope.cancelAnimationFrame = id => frames.delete(id);
    if (scope.WebGL2RenderingContext) {
        const canvas = scope.HTMLCanvasElement.prototype;
        const getContext = canvas.getContext;
        canvas.getContext = function (...values) {
            const context = getContext.apply(this, values);
            if (context instanceof scope.WebGL2RenderingContext) contexts.add(context);
            return context;
        };
    }
    if (scope.GPUQueue) {
        const submit = scope.GPUQueue.prototype.submit;
        scope.GPUQueue.prototype.submit = function (...values) {
            const result = submit.apply(this, values);
            queues.add(this);
            return result;
        };
    }
    scope.__lastLightGPU = () => ({ submitted, completed, pending: pendingAt === null ? 0 : 1,
        peak: submitted ? 1 : 0, longest, pendingAge: pendingAt === null ? 0 : wallNow() - pendingAt });
    scope.__lastLightStep = async (milliseconds = 1000 / 30) => {
        if (!Number.isFinite(milliseconds) || milliseconds < 0) throw new RangeError('Invalid frame duration');
        if (stepping) throw new Error('A browser frame is already running');
        if (!frames.size) return false;
        stepping = true;
        now += milliseconds;
        const batch = [...frames];
        try {
            for (const [id, callback] of batch) {
                if (!frames.delete(id)) continue;
                callback.call(scope, now);
            }
            if (contexts.size || queues.size) {
                const used = [...queues];
                queues.clear();
                submitted++;
                pendingAt = wallNow();
                try {
                    // WebGL has no GPUQueue promise. Complete its work before
                    // the runner can submit another software-rendered frame.
                    for (const context of contexts) context.finish();
                    await Promise.all(used.map(queue => queue.onSubmittedWorkDone()));
                    completed++;
                } finally {
                    longest = Math.max(longest, wallNow() - pendingAt);
                    pendingAt = null;
                }
            }
            return true;
        } finally {
            stepping = false;
        }
    };
}
