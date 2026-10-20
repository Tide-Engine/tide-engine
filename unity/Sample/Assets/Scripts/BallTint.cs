using Tide;
using UnityEngine;

// On the ball prefab: Tide's Ball (a type generated from the game's schema) as Unity draws it. The color is the
// match's, the size says which ball is this machine's player's.
[RequireComponent(typeof(Renderer))]
public sealed class BallTint : MonoBehaviour
{
    TideEntityView view;
    Renderer shown;
    MaterialPropertyBlock block;

    void Awake()
    {
        view = GetComponent<TideEntityView>();
        shown = GetComponent<Renderer>();
        block = new MaterialPropertyBlock();
    }

    void LateUpdate()
    {
        if (view == null) view = GetComponent<TideEntityView>();
        if (view == null || !view.TryGet(out Tide.Game.Ball ball)) return;
        block.SetColor("_Color", ball.color);
        block.SetColor("_BaseColor", ball.color);
        shown.SetPropertyBlock(block);
        transform.localScale = Vector3.one * (view.Mine ? 1.1f : 0.9f);
    }
}
