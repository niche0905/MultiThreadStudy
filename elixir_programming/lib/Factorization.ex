
defmodule Factorization do
  def factorize_ordinary(n, divisior \\ 2, factors \\ []) do
    cond do
      n < 2 -> factors
      rem(n, divisior) == 0 -> factorize_ordinary(div(n, divisior), divisior, [divisior | factors])
      true -> factorize_ordinary(n, divisior + 1, factors)
    end
  end

  def factorize_single(n) do
    max_divisior = :math.sqrt(n) |> trunc()
    numbers = Enum.to_list(2..max_divisior)
    factorize_single(n, numbers, [])
  end
  defp factorize_single(1, _divisiors, factors), do: Enum.reverse(factors)
  defp factorize_single(n, [], factors), do: Enum.reverse([n | factors])

  defp factorize_single(n, [p | rest], factors) do
    cond do
      rem(n, p) == 0 ->
        factorize_single(div(n, p), [p | rest], [p | factors])
      true ->
        filtered_list = Enum.reject(rest, &(rem(&1, p) == 0))
        factorize_single(n, filtered_list, factors)
    end
  end


end
