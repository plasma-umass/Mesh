\begin{minted}[frame=single]{c++}
void MiniHeap::localFree(void *ptr, RNG &rng) {
  const ssize_t freedOff = getOff(ptr);
  if (freedOff < 0)
    return;
  _freeList[--_off] = freedOff;
  size_t swapOff =
    rng.inRange(_off, maxCount() - 1);
  // move the newly freed pointer somewhere
  // in the freelist
  swap(_freeList[_off], _freeList[swapOff]);
  _bitmap.unset(freedOff);
}
\end{minted}
