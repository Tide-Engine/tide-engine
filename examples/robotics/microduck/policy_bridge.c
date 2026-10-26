#include <stdint.h>
#include <string.h>

#include "tide/list.h"
#include "policy/inference.h"

// Tide flattens the read-only input and owns the output list. The actor has
// no persistent native state; action and filter history live in the match.
int32_t DuckVelStandInfer(const float *observation, int32_t count, tide_list_out *actions)
{
    if (count != MD_POLICY_OBSERVATION_COUNT || !observation || !actions) return 0;
    float result[MD_POLICY_ACTION_COUNT];
    if (!md_policy_evaluate(observation, result)) return 0;
    float *output = tide_list_out_room(actions, MD_POLICY_ACTION_COUNT);
    if (!output) return 0;
    memcpy(output, result, sizeof(result));
    tide_list_out_count(actions, MD_POLICY_ACTION_COUNT);
    return 1;
}
