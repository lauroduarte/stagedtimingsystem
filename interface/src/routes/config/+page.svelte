<script lang="ts">
  // Esses valores viriam de uma store ou API no projeto real
  let nomeProva = "Enduro Vale Selvagem 2025";
  let categorias = ["Elite", "Júnior", "Master A", "Master B"];
  let totalPilotos = 42;
  let relogio = new Date().toLocaleString();
  let urlSync = "http://192.168.0.100/sync";
</script>

<div class="space-y-6">
  <h1 class="text-2xl font-bold">Resumo das Configurações</h1>

  <div class="grid md:grid-cols-2 gap-6">
    <div class="bg-white p-4 rounded shadow">
      <h2 class="text-lg font-semibold">Prova</h2>
      <p class="text-gray-700">{nomeProva}</p>
    </div>

    <div class="bg-white p-4 rounded shadow">
      <h2 class="text-lg font-semibold">Categorias</h2>
      <ul class="list-disc ml-5 text-gray-700">
        {#each categorias as cat}
          <li>{cat}</li>
        {/each}
      </ul>
    </div>

    <div class="bg-white p-4 rounded shadow">
      <h2 class="text-lg font-semibold">Total de Pilotos</h2>
      <p class="text-gray-700">{totalPilotos}</p>
    </div>

    <div class="bg-white p-4 rounded shadow">
      <h2 class="text-lg font-semibold">Relógio</h2>
      <p class="text-gray-700">{relogio}</p>
    </div>

    <div class="bg-white p-4 rounded shadow md:col-span-2">
      <h2 class="text-lg font-semibold">URL de Sincronização</h2>
      <p class="text-blue-600 break-all">{urlSync}</p>
    </div>
  </div>
</div>