\begin{minted}[frame=single]{c++}
void *MeshLocal::malloc(size_t sz) {
  int szClass = getSizeClass(sz);
  auto szMax = getClassMaxSize(szClass);
  // forward to global heap if large object
  if (szMax > _maxObjectSz)
    return _global->malloc(sz);
  // allocate MiniHeap if we don't have one
  if (_current[szClass] == nullptr)
    _current[szClass] =
      _global->allocMiniheap(szMax);
  MiniHeap *mh = _current[szClass];
  void *ptr = mh->malloc(szMax);
  if (mh->isExhausted()) {
    mh->detach(); // now full
    _current[szClass] = nullptr;
  }
  return ptr;
}
\end{minted}
