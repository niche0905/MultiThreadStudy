
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

  def factorize_multi(n) do
    max_divisor = :math.sqrt(n) |> trunc()
    numbers = Enum.to_list(2..max_divisor)

    # 재귀로 반복하면서 numbers에서 head를 꺼내고 numbers를 수정하고
    # 프로세스 만들어서 head로 나누어 떨어지면 인수로 추가하고
    factorize_multi(n, numbers, [])
  end

  defp factorize_multi(1, _divisiors, factors), do: factors
  defp factorize_multi(n, [], factors) when n != 1, do: factors ++ [n]

  defp factorize_multi(n, [p | rest], factors) do
    # 프로세스 만들어서 p로 나누어 떨어지면 인수로 p 추가 한 리스트 리턴
    parent = self()

    spawn(fn  ->
      result = factorize_recursive(n, p, [])
      send(parent, {:result, div(n, Enum.reduce(result, 1, &(&1 * &2))), result})
     end)

    filtered_list = Enum.reject(rest, &(rem(&1, p) == 0))
    receive do
      {:result, new_n, result} ->
        factorize_multi(new_n, filtered_list, factors ++ result)
    end
  end

  defp factorize_recursive(n, p, factors) do
    if rem(n, p) == 0 do
      factorize_recursive(div(n, p), p, [p | factors])
    else
      factors
    end
  end

end
