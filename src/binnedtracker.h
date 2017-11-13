// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-

#pragma once
#ifndef MESH__BINNEDTRACKER_H
#define MESH__BINNEDTRACKER_H

#include <mutex>

#include "internal.h"

// invariants:
// - MiniHeap only in one one bin in one BinnedTracker

// want:
// - 'side-bins' for full + empty spans
// - $BinCount partially full bins

// TODO:
// - track _highWaterMark

namespace mesh {

template <typename MiniHeap, typename GlobalHeap, size_t BinCount = 4, size_t MaxEmpty = 128>
class BinnedTracker {
private:
  DISALLOW_COPY_AND_ASSIGN(BinnedTracker);

public:
  BinnedTracker() : _prng(internal::seed()) {
  }

  // give the BinnedTracker a back-pointer to its owner, the global heap
  void init(GlobalHeap *heap) {
    _heap = heap;
  }

  size_t objectCount() const {
    if (unlikely(!_hasMetadata))
      mesh::debug("BinnedTracker.objectCount() called before set");
    return _objectCount;
  }

  size_t objectSize() const {
    if (unlikely(!_hasMetadata))
      mesh::debug("BinnedTracker.objectSize() called before set");
    return _objectSize;
  }

private:
  // remove and return a MiniHeap uniformly at random from the given vector
  MiniHeap *popRandomLocked(internal::vector<MiniHeap *> &vec) {
    std::uniform_int_distribution<size_t> distribution(0, vec.size() - 1);
    const size_t off = distribution(_prng);

    MiniHeap *mh = vec[off];
    // when we pop for reuse, we effectively "top off" a MiniHeap, so
    // it moves into the full bin
    move(_full, vec, mh, internal::BinToken::FlagFull);

    return mh;
  }

public:
#if 0
  MiniHeap *selectForReuse() {
    std::lock_guard<std::mutex> lock(_mutex);

    const auto partialCount = partialSize();

    // no partial miniheaps means we should reuse an empty span
    if (partialCount == 0) {
      if (_empty.size() == 0)
        return nullptr;

      return popRandomLocked(_empty);
    }

    std::uniform_int_distribution<size_t> distribution(0, partialCount - 1);
    const size_t off = distribution(_prng);

    size_t count = 0;
    for (size_t i = 0; i < BinCount; i++) {
      count += _partial[i].size();
      if (off < count)
        return popRandomLocked(_partial[i]);
    }

    mesh::debug("selectForReuse: should be unreachable");
    return nullptr;
  }
#else
  MiniHeap *selectForReuse() {
    std::lock_guard<std::mutex> lock(_mutex);

    for (int i = BinCount - 1; i >= 0; i--) {
      while (_partial[i].size() > 0) {
        auto mh = popRandomLocked(_partial[i]);
        if (unlikely(mh->inUseCount() == mh->maxCount())) {
          mesh::debug("skipping exhausted partial?");
          continue;
        }
        return mh;
      }
    }

    return nullptr;
  }
#endif

  internal::vector<MiniHeap *> meshingCandidates(double occupancyCutoff) const {
    std::lock_guard<std::mutex> lock(_mutex);

    internal::vector<MiniHeap *> bucket{};

    // consider all of our partially filled miniheaps
    for (size_t i = 0; i < BinCount; i++) {
      const auto partial = _partial[i];
      for (size_t j = 0; j < partial.size(); j++) {
        const auto mh = partial[j];
        if (mh->isMeshingCandidate() && (mh->fullness() < occupancyCutoff))
          bucket.push_back(mh);
      }
    }

    return bucket;
  }

  // called after a free through the global heap has happened
  void postFree(MiniHeap *mh) {
    const auto inUseCount = mh->inUseCount();
    if (unlikely(inUseCount == _objectCount))
      return;

    if (mh->isAttached())
      return;

    uint32_t oldBinId;
    uint32_t newBinId;

    {
      std::lock_guard<std::mutex> lock(_mutex);

      oldBinId = mh->getBinToken().bin();
      newBinId = getBinId(inUseCount);

      if (likely(newBinId == oldBinId))
        return;

      move(getBin(newBinId), getBin(oldBinId), mh, newBinId);

      if (newBinId != internal::BinToken::FlagEmpty || _empty.size() < MaxEmpty)
        return;
    }

    // must be called without lock held
    flushFreeMiniheaps();
  }

  void add(MiniHeap *mh) {
    std::lock_guard<std::mutex> lock(_mutex);

    d_assert(mh != nullptr);

    if (unlikely(!_hasMetadata))
      setMetadata(mh);

    mh->setBinToken(internal::BinToken::Full());
    addTo(_full, mh);
  }

  void remove(MiniHeap *mh) {
    std::lock_guard<std::mutex> lock(_mutex);

    if (unlikely(!mh->getBinToken().valid())) {
      mesh::debug("ERROR: bad bin token");
      return;
    }

    const auto bin = mh->getBinToken().bin();
    removeFrom(getBin(bin), mh);
  }

  size_t allocatedObjectCount() const {
    std::lock_guard<std::mutex> lock(_mutex);

    size_t sz = 0;

    for (size_t i = 0; i < _full.size(); i++) {
      if (_full[i] != nullptr)
        sz += _full[i]->inUseCount();
    }

    for (size_t i = 0; i < BinCount; i++) {
      auto partial = _partial[i];
      for (size_t j = 0; j < partial.size(); j++) {
        MiniHeap *mh = partial[j];
        if (mh != nullptr && !mh->isAttached())
          sz += mh->inUseCount();
      }
    }

    return sz;
  }

  // number of MiniHeaps we are tracking
  size_t count() const {
    return nonEmptyCount() + _empty.size();
  }

  size_t nonEmptyCount() const {
    return _full.size() + partialSize();
  }

  size_t partialSize() const {
    size_t sz = 0;
    for (size_t i = 0; i < BinCount; i++) {
      sz += _partial[i].size();
    }
    return sz;
  }

  void printOccupancy() const {
    std::lock_guard<std::mutex> lock(_mutex);

    for (size_t i = 0; i < _full.size(); i++) {
      if (_full[i] != nullptr)
        _full[i]->printOccupancy();
    }

    for (size_t i = 0; i < BinCount; i++) {
      auto partial = _partial[i];
      for (size_t j = 0; j < partial.size(); j++) {
        MiniHeap *mh = partial[j];
        if (mh != nullptr && !mh->isAttached())
          mh->printOccupancy();
      }
    }

    // no need to print occupancy of _empty MiniHeaps
  }

  void dumpStats(bool beDetailed) const {
    std::lock_guard<std::mutex> lock(_mutex);

    const auto mhCount = count();

    if (mhCount == 0) {
      debug("MH HWM (%5zu : %3zu):     %6zu/%6zu\n", _objectSize.load(), 0, 0, _highWaterMark.load());
      return;
    }

    const auto totalObjects = mhCount * _objectCount;

    double inUseCount = 0;

    for (size_t i = 0; i < _full.size(); i++) {
      if (_full[i] != nullptr) {
        inUseCount += _full[i]->inUseCount();
        // if (beDetailed && size == 4096)
        //   debug("\t%5.2f\t%s\n", mh->fullness(), mh->bitmap().to_string().c_str());
      }
    }

    for (size_t i = 0; i < BinCount; i++) {
      auto partial = _partial[i];
      for (size_t j = 0; j < partial.size(); j++) {
        MiniHeap *mh = partial[j];
        if (mh != nullptr)
          inUseCount += mh->inUseCount();
      }
    }

    debug("MH HWM (%5zu : %3zu):     %6zu/%6zu (occ: %f)\n", _objectSize.load(), totalObjects, mhCount,
          _highWaterMark.load(), inUseCount / totalObjects);
  }

  void flushFreeMiniheaps() {
    internal::vector<MiniHeap *> toFree;

    // ensure we don't call back into the GlobalMeshingHeap with our lock held
    {
      std::lock_guard<std::mutex> lock(_mutex);

      for (size_t i = 0; i < _empty.size(); i++) {
        toFree.push_back(_empty[i]);
      }

      _empty.clear();
    }

    d_assert(_heap != nullptr);
    for (size_t i = 0; i < toFree.size(); i++) {
      _heap->freeMiniheap(toFree[i], false);
    }
  }

private:
  void setMetadata(MiniHeap *mh) {
    _objectCount = mh->maxCount();
    _objectSize = mh->objectSize();
    _hasMetadata = true;
  }

  void swapTokens(MiniHeap *mh1, MiniHeap *mh2) {
    const auto temp = mh1->getBinToken();
    mh1->setBinToken(mh2->getBinToken());
    mh2->setBinToken(temp);
  }

  void move(internal::vector<MiniHeap *> &to, internal::vector<MiniHeap *> &from, MiniHeap *mh, uint32_t size) {
    removeFrom(from, mh);
    mh->setBinToken(internal::BinToken(size, internal::BinToken::FlagNoOff));
    addTo(to, mh);
  }

  // must be called with _mutex held
  void addTo(internal::vector<MiniHeap *> &vec, MiniHeap *mh) {
    const size_t endOff = vec.size();

    mh->setBinToken(mh->getBinToken().newOff(endOff));
    vec.push_back(mh);

    // endpoint is _inclusive_, so we subtract 1 from size since we're
    // dealing with 0-indexed offsets
    std::uniform_int_distribution<size_t> distribution(0, vec.size() - 1);

    const size_t swapOff = distribution(_prng);

    std::swap(vec[swapOff], vec[endOff]);
    swapTokens(vec[swapOff], vec[endOff]);
  }

  // must be called with _mutex held
  void removeFrom(internal::vector<MiniHeap *> &vec, MiniHeap *mh) {
    // a bug if we try to remove a miniheap from an empty vector
    d_assert(vec.size() > 0);

    const size_t off = mh->getBinToken().off();
    const size_t endOff = vec.size() - 1;

    vec[endOff]->setBinToken(mh->getBinToken());

    // move our miniheap to the last element, then pop that last element
    std::swap(vec[off], vec[endOff]);

    vec[endOff] = nullptr;
    vec.pop_back();

    // for (size_t i = 0; i < vec.size(); i++) {
    //   if (vec[i] == mh) {
    //     mesh::debug("!!!! not actually removed?");
    //     mh->dumpDebug();
    //     abort();
    //   }
    // }

    // update the miniheap's token
    mh->setBinToken(mh->getBinToken().newOff(internal::BinToken::FlagNoOff));
  }

  uint32_t getBinId(uint32_t inUseCount) const {
    if (inUseCount == _objectCount) {
      return internal::BinToken::FlagFull;
    } else if (inUseCount == 0) {
      return internal::BinToken::FlagEmpty;
    } else {
      return (inUseCount * BinCount)/_objectCount;
    }
  }

  internal::vector<MiniHeap *> &getBin(const uint32_t bin) {
    if (bin == internal::BinToken::FlagFull) {
      return _full;
    } else if (bin == internal::BinToken::FlagEmpty) {
      return _empty;
    } else {
      return _partial[bin];
    }
  }

  GlobalHeap *_heap;

  internal::vector<MiniHeap *> _full;
  internal::vector<MiniHeap *> _partial[BinCount];
  internal::vector<MiniHeap *> _empty;

  atomic_size_t _objectSize{0};
  atomic_size_t _objectCount{0};

  atomic_size_t _highWaterMark{0};

  mt19937_64 _prng;
  mutable std::mutex _mutex{};

  bool _hasMetadata{false};
};
}  // namespace mesh

#endif  // MESH__BINNEDTRACKER_H
