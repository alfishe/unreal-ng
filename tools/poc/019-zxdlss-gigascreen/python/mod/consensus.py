"""Consensus: one recipe per pixel from the proposals of a stage.

score = confidence x stage prior; a proposal counts where score >= threshold,
the pixel is free (not explained by an earlier stage) and no veto covers it.
Among counting proposals the lowest rank wins (the smallest period), then the
highest score. The winner id per pixel is kept for the debug panel.
"""
import numpy as np

from python.mod.registry import Proposal, Veto


def consensus(items, free, prior=1.0, threshold=0.5):
    """-> (claimed mask, weights {frame index: H x W}, winner index per pixel, detector names)."""
    shape = free.shape
    vetoed = np.zeros(shape, bool)
    for it in items:
        if isinstance(it, Veto):
            vetoed |= it.mask
    proposals = [it for it in items if isinstance(it, Proposal)]
    best_rank = np.full(shape, np.iinfo(np.int16).max, np.int32)
    best_score = np.zeros(shape, np.float32)
    winner = np.full(shape, -1, np.int16)
    for k, pr in enumerate(proposals):
        score = pr.confidence * prior
        rank = pr.rank if pr.rank is not None else np.zeros(shape, np.int16)
        ok = pr.mask & free & ~vetoed & (score >= threshold)
        better = ok & ((rank < best_rank) | ((rank == best_rank) & (score > best_score)))
        best_rank[better] = rank[better]
        best_score[better] = score[better]
        winner[better] = k
    claimed = winner >= 0
    weights = {}
    for k, pr in enumerate(proposals):
        mk = winner == k
        if not mk.any():
            continue
        for i, w in pr.weights.items():
            weights.setdefault(i, np.zeros(shape))
            weights[i][mk] = w[mk]
    return claimed, weights, winner, [p.detector for p in proposals]
