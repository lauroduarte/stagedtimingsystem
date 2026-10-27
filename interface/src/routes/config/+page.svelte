<script lang="ts">
	import type { Prova, Categoria, Atleta, Relogio } from '$lib/types/models';
	import { getAtletas, getCategorias, getProva, getRelogio } from '$lib/services/services';

	// Find and display configuration summary for

	// Prova
// 	getProva().then((prova) => {
// 		if (!prova) {
// 			console.error('Prova not found');
// 			return;
// 		}
// 		provaAtual = prova;
// 	});

//   getCategorias().then((cats) => {
//     if (!cats) {
//       console.error('Categorias not found');
//       return;
//     }
//     categorias = cats;
//   });

//   getAtletas().then((ats) => {
//     if (!ats) {
//       console.error('Atletas not found');
//       return;
//     }
//     atletas = ats;
//   });

//   getRelogio().then((clock) => {
//     if (!clock) {
//       console.error('Relógio not found');
//       return;
//     }
//     relogio = clock;
//   });
  
	// Set default Prova
	let provaAtual: Prova = { nome: 'Prova de Exemplo', local: 'Local de Exemplo', data: '2023-10-01' };
  let categorias: Categoria[] =  [{ id: 1, categoria: 'Categoria Exemplo' }];
  let atletas: Atleta[] = [{ nome: 'Piloto Exemplo', equipe: 'Equipe Exemplo', numeral: 1, categoriaId: 1 }];
	let relogio: Relogio = { datetime: new Date().toISOString() };
	let urlSync = 'http://192.168.0.100/sync';
</script>

<div class="space-y-6">
	<h1 class="text-2xl font-bold">Resumo das Configurações</h1>

	<div class="grid md:grid-cols-2 gap-6">
		<div class="bg-white p-4 rounded shadow">
			<h2 class="text-lg font-semibold">Prova</h2>
			<p class="text-gray-700">{provaAtual.nome}</p>
		</div>

		<div class="bg-white p-4 rounded shadow">
			<h2 class="text-lg font-semibold">Local e Data</h2>
			<p class="text-gray-700">{provaAtual.local} - {provaAtual.data}</p>
		</div>

		<div class="bg-white p-4 rounded shadow">
			<h2 class="text-lg font-semibold">Categorias</h2>
			<ul class="list-disc ml-5 text-gray-700">
				{#each categorias as cat}
					<li>{cat.categoria}</li>
				{/each}
			</ul>
		</div>

		<div class="bg-white p-4 rounded shadow">
			<h2 class="text-lg font-semibold">Total de Pilotos</h2>
			<p class="text-gray-700">{atletas.length}</p>
		</div>

		<div class="bg-white p-4 rounded shadow">
			<h2 class="text-lg font-semibold">Relógio</h2>
			<p class="text-gray-700">{relogio.datetime}</p>
		</div>

		<div class="bg-white p-4 rounded shadow md:col-span-2">
			<h2 class="text-lg font-semibold">URL de Sincronização</h2>
			<p class="text-blue-600 break-all">{urlSync}</p>
		</div>
	</div>
</div>
