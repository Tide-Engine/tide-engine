// Serialized into the playtest's pages, so this function has no module bindings.
export function installFramePacing(scope) {
    const requestFrame = scope.requestAnimationFrame.bind(scope);
    const cancelFrame = scope.cancelAnimationFrame.bind(scope);
    const frameRequests = new Map();
    const frameQueues = new Set();
    const gpuFrames = new Map();
    let frameRequest = 0, nextFrame = 0;
    let gpuSubmitted = 0, gpuCompleted = 0, gpuPeak = 0, gpuLongest = 0;
    let gpuWait = Promise.resolve();
    if (scope.GPUQueue) {
        const submit = scope.GPUQueue.prototype.submit;
        scope.GPUQueue.prototype.submit = function (...values) {
            const result = submit.apply(this, values);
            frameQueues.add(this);
            return result;
        };
    }
    scope.__lastLightGPU = () => ({
        submitted: gpuSubmitted, completed: gpuCompleted, pending: gpuFrames.size,
        peak: gpuPeak, longest: gpuLongest,
        pendingAge: gpuFrames.size ? scope.performance.now() - Math.min(...gpuFrames.values()) : 0,
    });
    const submittedFrame = () => {
        if (!frameQueues.size) return;
        const id = ++gpuSubmitted, started = scope.performance.now();
        gpuFrames.set(id, started);
        gpuPeak = Math.max(gpuPeak, gpuFrames.size);
        const queues = [...frameQueues];
        frameQueues.clear();
        gpuWait = Promise.all(queues.map(queue => queue.onSubmittedWorkDone())).then(() => {
            gpuCompleted++;
        }, error => {
            // The runner treats console errors as failures. Release the wait
            // as well, so a rejected fence does not hide behind a frame timeout.
            scope.console.error('GPU completion failed: ' + error);
        }).finally(() => {
            gpuFrames.delete(id);
            gpuLongest = Math.max(gpuLongest, scope.performance.now() - started);
        });
    };
    scope.requestAnimationFrame = callback => {
        if (typeof callback !== 'function') throw new TypeError('Animation frame callback must be a function');
        const id = ++frameRequest;
        const request = { timer: null, frame: null };
        const schedule = () => {
            if (!frameRequests.has(id)) return;
            if (gpuFrames.size) {
                gpuWait.then(schedule);
                return;
            }
            const remaining = nextFrame - scope.performance.now();
            if (remaining > 0) request.timer = scope.setTimeout(schedule, remaining);
            else request.frame = requestFrame(timestamp => {
                frameRequests.delete(id);
                try { callback.call(scope, timestamp); }
                finally {
                    // WebGPU submission returns before software rendering
                    // finishes. Keep at most one game frame in flight and
                    // preserve the WebGL test's minimum JS idle interval.
                    submittedFrame();
                    nextFrame = scope.performance.now() + 1000 / 30;
                }
            });
        };
        frameRequests.set(id, request);
        request.timer = scope.setTimeout(schedule, Math.max(0, nextFrame - scope.performance.now()));
        return id;
    };
    scope.cancelAnimationFrame = id => {
        const request = frameRequests.get(id);
        if (!request) return;
        scope.clearTimeout(request.timer);
        if (request.frame !== null) cancelFrame(request.frame);
        frameRequests.delete(id);
    };
}
