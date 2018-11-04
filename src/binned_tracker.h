// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-

#pragma once
#ifndef MESH__BINNED_TRACKER_H
#define MESH__BINNED_TRACKER_H

#include <mutex>

#include "internal.h"

#include "rng/mwc.h"

// invariants:
// - MiniHeap only in one one bin in one BinnedTracker

// want:
// - 'side-bins' for full + empty spans
// - $BinCount partially full bins

// TODO:
// - track _highWaterMark

namespace mesh {

template <typename MiniHeap>
class BinnedTracker {
private:
  DISALLOW_COPY_AND_ASSIGN(BinnedTracker);

public:
  BinnedTracker() : _fastPrng(internal::seed(), internal::seed()) {
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

    const size_t off = _fastPrng(0, partialCount - 1);

    size_t count = 0;
    for (size_t i = 0; i < kBinnedTrackerBinCount; i++) {
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

    for (int i = kBinnedTrackerBinCount - 1; i >= 0; i--) {
      while (_partial[i].size() > 0) {
        auto mh = popRandomLocked(_partial[i]);
        // if we didn't find something in popRandom, break out of the
        // while loop and try the next fullness size
        if (unlikely(mh == nullptr)) {
          break;
        }

        d_assert(!mh->isAttached());

        // this can happen because in use count is updated outside the
        // bin tracker lock -- it may be queued up for reuse.
        if (unlikely(mh->isFull() || mh->isAttached())) {
          debug("I don't know how this could happen");
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
    for (size_t i = 0; i < kBinnedTrackerBinCount; i++) {
      const auto partial = _partial[i];
      if (i == kBinnedTrackerBinCount / 2 + 1 && bucket.size() == 0) {
        break;
      }
      for (size_t j = 0; j < partial.size(); j++) {
        const auto mh = partial[j];
        if (mh->isMeshingCandidate() && (mh->fullness() < occupancyCutoff))
          bucket.push_back(mh);
      }
    }

    return bucket;
  }

  // called after a free through the global heap has happened --
  // miniheap must be unreffed by return
  bool postFree(MiniHeap *mh, uint32_t inUseCount) {
    if (mh->isAttached() > 0) {
      return false;
    }

    std::atomic_thread_fence(std::memory_order_acquire);

    auto oldBinId = mh->getBinToken().bin();
    auto newBinId = getBinId(inUseCount);

    if (likely(newBinId == oldBinId))
      return false;

    std::lock_guard<std::mutex> lock(_mutex);

    // double-check
    oldBinId = mh->getBinToken().bin();
    newBinId = getBinId(mh->inUseCount());
    if (unlikely(newBinId == oldBinId))
      return false;

    move(getBin(newBinId), getBin(oldBinId), mh, newBinId);

    return newBinId == internal::bintoken::FlagEmpty && _empty.size() >= kBinnedTrackerMaxEmpty;
  }

  void add(MiniHeap *mh) {
    std::lock_guard<std::mutex> lock(_mutex);

    d_assert(mh != nullptr);

    if (unlikely(!_hasMetadata))
      setMetadata(mh);

    mh->setBinToken(internal::BinToken::Full());
    addTo(_full, mh);
    std::atomic_thread_fence(std::memory_order_release);
  }

  void remove(MiniHeap *mh) {
    std::lock_guard<std::mutex> lock(_mutex);

    if (unlikely(!mh->getBinToken().valid())) {
      mesh::debug("ERROR: bad bin token");
      d_assert(false);
      return;
    }

    const auto bin = mh->getBinToken().bin();
    removeFrom(getBin(bin), mh);
    std::atomic_thread_fence(std::memory_order_release);
  }

  size_t allocatedObjectCount() const {
    std::lock_guard<std::mutex> lock(_mutex);

    size_t sz = 0;

    for (size_t i = 0; i < _full.size(); i++) {
      if (_full[i] != nullptr)
        sz += _full[i]->inUseCount();
    }

    for (size_t i = 0; i < kBinnedTrackerBinCount; i++) {
      auto partial = _partial[i];
      for (size_t j = 0; j < partial.size(); j++) {
        MiniHeap *mh = partial[j];
        if (mh != nullptr)
          sz += mh->inUseCount();
      }
    }

    return sz;
  }

  // number of MiniHeaps we are tracking
  size_t count() const {
    std::lock_guard<std::mutex> lock(_mutex);

    return _empty.size() + _full.size() + partialSizeLocked();
  }

  size_t nonEmptyCount() const {
    std::lock_guard<std::mutex> lock(_mutex);

    return _full.size() + partialSizeLocked();
  }

  size_t partialSize() const {
    std::lock_guard<std::mutex> lock(_mutex);

    return partialSizeLocked();
  }

  size_t partialSizeLocked() const {
    size_t sz = 0;
    for (size_t i = 0; i < kBinnedTrackerBinCount; i++) {
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

    for (size_t i = 0; i < kBinnedTrackerBinCount; i++) {
      auto partial = _partial[i];
      for (size_t j = 0; j < partial.size(); j++) {
        MiniHeap *mh = partial[j];
        if (mh != nullptr)
          mh->printOccupancy();
      }
    }

    // no need to print occupancy of _empty MiniHeaps
  }

  void dumpStats(bool beDetailed) const {
    std::lock_guard<std::mutex> lock(_mutex);

    const auto mhCount = count();

    if (mhCount == 0) {
      debug("MH HWM (%5zu : %5zu):     %6zu/%6zu\n", _objectSize.load(), 0, 0, _highWaterMark.load());
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

    for (size_t i = 0; i < kBinnedTrackerBinCount; i++) {
      auto partial = _partial[i];
      for (size_t j = 0; j < partial.size(); j++) {
        MiniHeap *mh = partial[j];
        if (mh != nullptr)
          inUseCount += mh->inUseCount();
      }
    }

    debug("MH HWM (%5zu : %5zu):     %6zu/%6zu (occ: %f)\n", _objectSize.load(), totalObjects, mhCount,
          _highWaterMark.load(), inUseCount / totalObjects);
  }

  internal::vector<MiniHeap *> getFreeMiniheaps() {
    std::lock_guard<std::mutex> lock(_mutex);

    internal::vector<MiniHeap *> toFree;

    for (size_t i = 0; i < _empty.size(); i++) {
      toFree.push_back(_empty[i]);
    }

    _empty.clear();

    return toFree;
  }

private:
  // remove and return a MiniHeap uniformly at random from the given vector
  MiniHeap *popRandomLocked(internal::vector<MiniHeap *> &vec) {
    for (size_t i = 0; i < 16; i++) {
      const size_t off = _fastPrng.inRange(0, vec.size() - 1);

      MiniHeap *mh = vec[off];
      // we hold the mhRWLock exclusively at this point.  If a
      // miniheap is attached to a freelist it is not a candidate for
      // allocation.
      if (unlikely(mh->isAttached())) {
        continue;
      }

      // when we pop for reuse, we effectively "top off" a MiniHeap, so
      // it moves into the full bin
      move(_full, vec, mh, internal::bintoken::FlagFull);

      return mh;
    }
    return nullptr;
  }

  void setMetadata(MiniHeap *mh) {
    _objectCount = mh->maxCount();
    _objectCountReciprocal = 1 / mh->maxCount();
    _objectSize = mh->objectSize();
    _hasMetadata = true;
  }

  void swapTokens(MiniHeap *mh1, MiniHeap *mh2) {
    const auto temp = mh1->getBinToken();
    mh1->setBinToken(mh2->getBinToken());
    mh2->setBinToken(temp);
    std::atomic_thread_fence(std::memory_order_release);
  }

  void move(internal::vector<MiniHeap *> &to, internal::vector<MiniHeap *> &from, MiniHeap *mh, uint32_t size) {
    removeFrom(from, mh);
    mh->setBinToken(internal::BinToken(size, internal::bintoken::FlagNoOff));
    addTo(to, mh);
    std::atomic_thread_fence(std::memory_order_release);
  }

  // must be called with _mutex held
  void addTo(internal::vector<MiniHeap *> &vec, MiniHeap *mh) {
    const size_t endOff = vec.size();

    mh->setBinToken(mh->getBinToken().newOff(endOff));
    vec.push_back(mh);

    // endpoint is _inclusive_, so we subtract 1 from size since we're
    // dealing with 0-indexed offsets
    const size_t swapOff = _fastPrng.inRange(0, vec.size() - 1);

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
    mh->setBinToken(mh->getBinToken().newOff(internal::bintoken::FlagNoOff));
  }

  uint32_t getBinId(uint32_t inUseCount) const {
    if (inUseCount == _objectCount) {
      return internal::bintoken::FlagFull;
    } else if (inUseCount == 0) {
      return internal::bintoken::FlagEmpty;
    } else {
      return (inUseCount * kBinnedTrackerBinCount) * _objectCountReciprocal;
    }
  }

  internal::vector<MiniHeap *> &getBin(const uint32_t bin) {
    if (bin == internal::bintoken::FlagFull) {
      return _full;
    } else if (bin == internal::bintoken::FlagEmpty) {
      return _empty;
    } else {
      return _partial[bin];
    }
  }

  atomic_size_t _objectSize{0};
  atomic_size_t _objectCount{0};
  float _objectCountReciprocal{0.0};

  atomic_size_t _highWaterMark{0};

  MWC _fastPrng;

  mutable std::mutex _mutex{};

  bool _hasMetadata{false};

  internal::vector<MiniHeap *> _full;
  internal::vector<MiniHeap *> _partial[kBinnedTrackerBinCount];
  internal::vector<MiniHeap *> _empty;
};
}  // namespace mesh

#endif  // MESH__BINNED_TRACKER_H
