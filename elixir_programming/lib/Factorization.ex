
defmodule Factorization do
  def factorize_ordinary(n) do
    start_time = System.monotonic_time(:microsecond)
    result = factorize_ordinary(n, 2, [])
    end_time = System.monotonic_time(:microsecond)
    elapsed_time = end_time - start_time
    IO.puts("Ordinary Execution time: #{elapsed_time} µs")
    result
  end

  def factorize_ordinary(n, divisior, factors) do
    cond do
      n < 2 -> factors
      rem(n, divisior) == 0 -> factorize_ordinary(div(n, divisior), divisior, [divisior | factors])
      true -> factorize_ordinary(n, divisior + 1, factors)
    end
  end

  def factorize_single(n) do
    start_time = System.monotonic_time(:microsecond)
    max_divisior = :math.sqrt(n) |> trunc()
    numbers = Enum.to_list(2..max_divisior)
    result = factorize_single(n, numbers, [])
    end_time = System.monotonic_time(:microsecond)
    elapsed_time = end_time - start_time
    IO.puts("Single Execution time: #{elapsed_time} µs")
    result
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
    start_time = System.monotonic_time(:microsecond)
    max_divisor = :math.sqrt(n) |> trunc()
    numbers = Enum.to_list(2..max_divisor)

    # 재귀로 반복하면서 numbers에서 head를 꺼내고 numbers를 수정하고
    # 프로세스 만들어서 head로 나누어 떨어지면 인수로 추가하고
    result = factorize_multi(n, numbers, [])
    end_time = System.monotonic_time(:microsecond)
    elapsed_time = end_time - start_time
    IO.puts("Multi Execution time: #{elapsed_time} µs")
    result
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

  def factorize_gpt(n) do
    start_time = System.monotonic_time(:microsecond)
    max_divisor = :math.sqrt(n) |> trunc()
    numbers = Enum.to_list(2..max_divisor)

    result = factorize_parallel(n, numbers, [])
    end_time = System.monotonic_time(:microsecond)
    elapsed_time = end_time - start_time
    IO.puts("Multi Execution time: #{elapsed_time} µs")
    result
  end

  defp factorize_parallel(n, [], factors) do
    if n > 1, do: [n | factors], else: factors
  end

  defp factorize_parallel(n, [p | rest], factors) do
    parent = self()

    task =
      Task.async(fn ->
        factorize_div(n, p, [])
      end)

    {new_n, new_factors} = Task.await(task, :infinity)

    filtered_list = Enum.reject(rest, &(rem(&1, p) == 0))

    factorize_parallel(new_n, filtered_list, new_factors ++ factors)
  end

  defp factorize_div(n, p, factors) do
    if rem(n, p) == 0 do
      factorize_div(div(n, p), p, [p | factors])
    else
      {n, factors}
    end
  end

end


"""
실행결과 주석문


iex(6)> Factorization.factorize_ordinary(120)
Ordinary Execution time: 0 µs
[5, 3, 2, 2, 2]
iex(7)> Factorization.factorize_single(120)
Single Execution time: 0 µs
[2, 2, 2, 3, 5]
iex(8)> Factorization.factorize_multi(120)
Multi Execution time: 102 µs
[2, 2, 2, 3, 5]
iex(9)> Factorization.factorize_gpt(120)
Multi Execution time: 103 µs
[5, 3, 2, 2, 2]

iex(10)> Factorization.factorize_ordinary(1213500)
Ordinary Execution time: 0 µs
[809, 5, 5, 5, 3, 2, 2]
iex(11)> Factorization.factorize_single(1213500)
Single Execution time: 103 µs
[2, 2, 3, 5, 5, 5, 809]
iex(12)> Factorization.factorize_multi(1213500)
Multi Execution time: 307 µs
[2, 2, 3, 5, 5, 5, 809]
iex(13)> Factorization.factorize_gpt(1213500)
Multi Execution time: 512 µs
[809, 5, 5, 5, 3, 2, 2]

iex(14)> Factorization.factorize_ordinary(52462044112)
Ordinary Execution time: 7363174 µs
[3278877757, 2, 2, 2, 2]
iex(15)> Factorization.factorize_single(52462044112)
Single Execution time: 2627380 µs
[2, 2, 2, 2, 3278877757]
iex(16)> Factorization.factorize_multi(52462044112)
Multi Execution time: 2644583 µs
[2, 2, 2, 2, 3278877757]
iex(17)> Factorization.factorize_gpt(52462044112)
Multi Execution time: 2583757 µs
[3278877757, 2, 2, 2, 2]

"""
