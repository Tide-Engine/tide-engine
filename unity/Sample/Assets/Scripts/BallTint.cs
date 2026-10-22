using Tide;
using UnityEngine;

// On the ball prefab: Tide's Ball, as Unity draws it. In Play its Ball component is the entity's (the runner writes
// the match's values into it each frame), so the color is the match's, and the size says which ball is this machine's
// player's.
[RequireComponent(typeof(Renderer))]
public sealed class BallTint : MonoBehaviour
{
    Tide.Game.Ball ball;
    TideEntityView view;
    Renderer shown;
    MaterialPropertyBlock block;

    void Awake()
    {
        shown = GetComponent<Renderer>();
        block = new MaterialPropertyBlock();
    }

    void LateUpdate()
    {
        if (ball == null) ball = GetComponent<Tide.Game.Ball>(); // The runner adds it as the entity shows here
        if (view == null) view = GetComponent<TideEntityView>();
        if (ball == null || view == null) return;
        block.SetColor("_Color", ball.color);
        block.SetColor("_BaseColor", ball.color);
        shown.SetPropertyBlock(block);
        transform.localScale = Vector3.one * (view.Mine ? 1.1f : 0.9f);
    }
}
