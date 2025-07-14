<script lang="ts">
	import Trash from '~icons/tabler/trash';
	import SettingsCard from '$lib/components/SettingsCard.svelte';
	import { page } from '$app/state';
	import { notifications } from '$lib/components/toasts/notifications';
	import { user } from '$lib/stores/user';
	import { on } from 'svelte/events';
	import { onMount } from 'svelte';
	import type { Atleta } from '$lib/types/models';
	import { getAtletas, getCategorias, salvarAtletas } from '$lib/services/services';

	let atletas = $state<Atleta[]>([{
		nome: '', 
		equipe: '', 
		numeral: 0,
		categoriaId: 0}]);

	let atleta = $state<Atleta>({
		nome: '',
		equipe: '',
		numeral: 0,
		categoriaId: 0});

	let categorias = $state([{
		id: 0, 
		categoria: ''}]);			

	onMount(() => {
		getAtletas()
			.then(atletasData => {
				atletas.splice(0, atletas.length, ...atletasData);
				atleta.numeral = atletas.length === 0 ? 1 : Math.max(0, ...atletas.map(a => a.numeral)) + 1;
			})
			.finally(() => {
				getCategorias().then(categoriasData => {
					categorias.splice(0, categorias.length, ...categoriasData);
				}).catch(error => {
					notifications.error('Erro ao carregar as categorias. Tente novamente.', 3000);
				});
			});
	});

	async function remover(numeral: number) {
		atletas = atletas.filter(item => item.numeral !== numeral);
		salvar();
	}

	async function adicionar() {
		if (atleta.nome.trim() === '' || atleta.equipe.trim() === '' || atleta.numeral <= 0) {
			notifications.error('Todos os campos devem ser preenchidos corretamente.', 3000);
			return;
		}

		if (atletas.some(a => a.numeral === atleta.numeral)) {
			notifications.error('Numeral já existe.', 3000);
			return;
		}

		atletas.push({...atleta});
		atleta = {nome: '', equipe: '', numeral: atletas.length === 0 ? 1 : Math.max(0, ...atletas.map(a => a.numeral)) + 1, categoriaId: 0};
		salvar();
		notifications.success('Atleta adicionado com sucesso!', 3000);
	}

	async function salvar() {
		try {
			const atletasData = await salvarAtletas(atletas);
			atletas.splice(0, atletas.length, ...atletasData);
			notifications.success('Atletas salvos com sucesso!', 3000);
		} catch (error) {
			console.error('Erro ao salvar os atletas:', error);
			notifications.error('Erro ao salvar a atleta. Tente novamente.', 3000);
		}
	}
</script>

<SettingsCard collapsible={false}>
	<div class="space-y-6">
		<h1 class="text-2xl font-bold">Cadastro da Atleta</h1>

		{#if atletas.length > 0}
			<table class="min-w-full bg-white rounded shadow mb-6">
				<thead>
					<tr>
						<th class="py-2 px-4 border-b font-semibold text-left">Nome</th>
						<th class="py-2 px-4 border-b font-semibold text-left">Equipe</th>
						<th class="py-2 px-4 border-b font-semibold text-left">Categoria</th>
						<th class="py-2 px-4 border-b font-semibold text-left">Numeral</th>
					</tr>
				</thead>
				<tbody>
					{#each atletas as item}
						<tr>
							<td class="py-2 px-4 border-b">{item.nome}</td>
							<td class="py-2 px-4 border-b">{item.equipe}</td>
							<td class="py-2 px-4 border-b">
								{#if categorias.length > 0}
									{#each categorias as categoria}
										{#if categoria.id === item.categoriaId}
											{categoria.categoria}
										{/if}
									{/each}
								{:else}
									Nenhuma categoria
								{/if}
							<td class="py-2 px-4 border-b">{item.numeral}</td>
							<td class="py-2 px-4 border-b">
								<button class="text-red-600 hover:text-red-800" on:click={() => remover(atleta.numeral)} aria-label="Remover categoria">
									<Trash />
								</button>
							</td>
						</tr>
					{/each}
				</tbody>
			</table>
		{:else}
			<p class="text-gray-500 mb-6">Nenhum atleta cadastrado.</p>
		{/if}

		<div class="bg-white p-6 rounded shadow max-w-xl">
			<label for="nome" class="block mb-2 font-medium">Nome da Atleta</label>
			<input
				id="nome"
				type="text"
				bind:value={atleta.nome}
				class="w-full border rounded px-3 py-2 mb-4 focus:outline-none focus:ring-2 focus:ring-blue-500"
			/>

			<label for="equipe" class="block mb-2 font-medium">Equipe</label>
			<input
				id="equipe"
				type="text"
				bind:value={atleta.equipe}
				class="w-full border rounded px-3 py-2 mb-4 focus:outline-none focus:ring-2 focus:ring-blue-500"
			/>

			<label for="categoria" class="block mb-2 font-medium">Categoria</label>
			<select	
				id="categoria"
				bind:value={atleta.categoriaId}
				class="w-full border rounded px-3 py-2 mb-4 focus:outline-none focus:ring-2 focus:ring-blue-500">
				<option value="0" disabled selected>Selecione uma categoria</option>
				{#each categorias as categoria}
					<option value={categoria.id}>{categoria.categoria}</option>
				{/each}
			</select>
			
			<label for="numeral" class="block mb-2 font-medium">Numeral</label>
			<input
				id="numeral"
				type="number"
				bind:value={atleta.numeral}
				class="w-full border rounded px-3 py-2 mb-4 focus:outline-none focus:ring-2 focus:ring-blue-500"
				min="1"
			/>

			<button
				on:click={adicionar}
				class="bg-blue-600 text-white px-4 py-2 rounded hover:bg-blue-700 transition"
			>
				Salvar
			</button>
		</div>
	</div>
</SettingsCard>
