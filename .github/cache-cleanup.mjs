// Retain two snapshots per compiler-cache family on the current trusted branch.
// Dependency downloads and other branches are outside this cleanup's scope.
export function planCacheCleanup(caches, ref, keep = 2) {
    if (!['refs/heads/dev', 'refs/heads/release'].includes(ref)) {
        throw new Error('Cache cleanup requires dev or release.');
    }
    if (!Number.isInteger(keep) || keep < 2) throw new Error('Keep at least two snapshots.');
    const families = new Map();
    for (const cache of caches) {
        const match = /^(ccache-.+-)[0-9a-f]{40}$/.exec(cache.key ?? '');
        if (cache.ref !== ref || !match) continue;
        if (!Number.isSafeInteger(cache.id) || !Number.isFinite(Date.parse(cache.created_at))) {
            throw new Error('Cache metadata has no valid ID or creation time.');
        }
        const family = families.get(match[1]) ?? [];
        family.push(cache);
        families.set(match[1], family);
    }
    const retained = [], removed = [];
    for (const family of families.values()) {
        family.sort((a, b) => Date.parse(b.created_at) - Date.parse(a.created_at) || b.id - a.id);
        retained.push(...family.slice(0, keep));
        removed.push(...family.slice(keep));
    }
    return { retained, removed };
}

export async function cleanupCompilerCaches(github, { owner, repo, ref }, log = console.log) {
    // Validate before making any requests, including requests from an unsafe ref.
    planCacheCleanup([], ref);
    const caches = await github.paginate(github.rest.actions.getActionsCacheList, {
        owner, repo, ref, per_page: 100,
    });
    const plan = planCacheCleanup(caches, ref);
    let deleted = 0, bytes = 0;
    for (const cache of plan.removed) {
        try {
            await github.rest.actions.deleteActionsCacheById({ owner, repo, cache_id: cache.id });
            deleted++;
            bytes += cache.size_in_bytes ?? 0;
            log(`Removed ${cache.key} (${cache.size_in_bytes ?? 0} bytes).`);
        } catch (error) {
            if (error.status !== 404) throw error; // Another cleanup may already have removed it.
        }
    }
    log(`Retained ${plan.retained.length} compiler snapshots; removed ${deleted} (${bytes} bytes).`);
    return { deleted, bytes, retained: plan.retained.length };
}
