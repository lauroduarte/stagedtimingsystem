<script lang="ts">
  type Especial = {
    tempo: string;
    gap: string;
    posicao: number;
  };

  type Resultado = {
    numero: number;
    nome: string;
    equipe: string;
    tempoTotal: string;
    gap: string;
    especiais: Especial[];
  };

  let prova: string = "Enduro Vale Selvagem 2025";

  let resultados: Resultado[] = [
    {
      numero: 21,
      nome: "João Pedro",
      equipe: "Team Alpha",
      tempoTotal: "08:32",
      gap: "+00:00",
      especiais: [
        { tempo: "02:10", gap: "+00:00", posicao: 1 },
        { tempo: "02:15", gap: "+00:00", posicao: 1 },
        { tempo: "02:07", gap: "+00:00", posicao: 1 }
      ]
    },
    {
      numero: 18,
      nome: "Carlos Silva",
      equipe: "Equipe Nitro",
      tempoTotal: "08:45",
      gap: "+00:13",
      especiais: [
        { tempo: "02:14", gap: "+00:04", posicao: 2 },
        { tempo: "02:19", gap: "+00:04", posicao: 2 },
        { tempo: "02:12", gap: "+00:05", posicao: 2 }
      ]
    }
  ];

  let expandido: number | null = null;

  function toggleExpand(i: number): void {
    expandido = expandido === i ? null : i;
  }
</script>

<div class="min-h-screen bg-gray-100 flex flex-col">
  <!-- Cabeçalho -->
  <header class="bg-white shadow p-4 flex items-center justify-between">
    <div class="flex items-center gap-4">
      <img src="/logo.png" alt="Logo" class="w-10 h-10" />
      <h1 class="text-2xl font-bold">Cronometragem MTB</h1>
    </div>
    <div class="text-gray-600 text-lg font-medium">
      {prova}
    </div>
  </header>

  <!-- Conteúdo principal -->
  <main class="flex-1 p-4">
    <div class="bg-white rounded shadow overflow-x-auto">
      <table class="w-full table-auto">
        <thead class="bg-gray-200 text-gray-700">
          <tr>
            <th class="p-2 text-left">#</th>
            <th class="p-2 text-left">Piloto</th>
            <th class="p-2 text-left">Equipe</th>
            <th class="p-2 text-left">Tempo</th>
            <th class="p-2 text-left">Gap</th>
            <th class="p-2">Ações</th>
          </tr>
        </thead>
        <tbody>
          {#each resultados as r, i}
            <tr class="border-t">
              <td class="p-2">{r.numero}</td>
              <td class="p-2">{r.nome}</td>
              <td class="p-2">{r.equipe}</td>
              <td class="p-2">{r.tempoTotal}</td>
              <td class="p-2">{r.gap}</td>
              <td class="p-2 text-center">
                <button on:click={() => toggleExpand(i)} class="text-blue-500 hover:underline">
                  {expandido === i ? 'Fechar' : 'Ver Tempos'}
                </button>
              </td>
            </tr>
            {#if expandido === i}
              <tr class="bg-gray-50">
                <td colspan="6" class="p-2 text-sm text-gray-700">
                  <div class="space-y-1">
                    {#each r.especiais as e, j}
                      <div>
                        Especial {j + 1}: Tempo {e.tempo}, Gap {e.gap}, Posição {e.posicao}
                      </div>
                    {/each}
                  </div>
                </td>
              </tr>
            {/if}
          {/each}
        </tbody>
      </table>
    </div>
  </main>

  <!-- Rodapé -->
  <footer class="bg-white border-t p-4 text-center">
    <a href="/config" class="text-blue-600 hover:underline font-medium">Configurações</a>
  </footer>
</div>
