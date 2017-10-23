\begin{minted}[frame=single]{c++}
void *MiniHeap::malloc(size_t sz) {
  const auto off = _freeList[_off++];
  _bitmap.set(off);
  return _spanStart + off * _objSize;
}
\end{minted}
