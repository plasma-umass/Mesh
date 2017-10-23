\begin{minted}[frame=single]{c++}
void MeshLocal::free(void *ptr) {
  // check if contained in attached MiniHeap
  for (size_t i = 0; i < NumBins; i++) {
    const auto curr = _current[i];
    if (curr && curr->contains(ptr)) {
      curr->localFree(ptr, _prng);
      return;
    }
  }
  _global->free(ptr); // general case
}
\end{minted}
