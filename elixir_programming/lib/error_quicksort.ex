defmodule ErrorQuicksort do
  def sort([]), do: []
  def sort ([pivot|t]) do
    sort (for x <- t, x < pivot, do: x)
    ++ [pivot] ++
    sort (for x <- t, x >= pivot, do: x)  # 문제가 되는 코드
  end
end
