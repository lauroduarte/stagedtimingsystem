<script lang="ts">
	import Trash from '~icons/tabler/trash';
	import SettingsCard from '$lib/components/SettingsCard.svelte';
	import { page } from '$app/state';
	import { notifications } from '$lib/components/toasts/notifications';
	import { user } from '$lib/stores/user';
	import { on } from 'svelte/events';
	import { onMount } from 'svelte';
	import type { Categoria } from '$lib/types/models';
	import { getCategorias, salvarCategorias } from '$lib/services/services';

	let categorias = $state<Categoria[]>([{
		id: 0, 
		categoria: ''}]);

	let nextId = $derived(() => {
		return categorias.length > 0 ? Math.max(...categorias.map(c => c.id)) + 1 : 1;
	});

	let categoria = $state('');

	onMount(async () => {
		// Carrega as categorias ao montar o componente
		try {
			const categoriasData = await getCategorias();
			categorias.splice(0, categorias.length, ...categoriasData);
		} catch (error) {
			notifications.error('Erro ao carregar as categorias. Tente novamente.', 3000);
		}
	});

	async function adicionar() {
		if (categoria.trim() === '') {
			notifications.error('O nome da categoria não pode estar vazio.', 3000);
			return;
		}

		if (categorias.some(c => c.categoria.toLowerCase() === categoria.toLowerCase())) {
			notifications.error('Categoria já existe.', 3000);
			return;
		}

		categorias.push({
			id: nextId(),
			categoria: categoria
		});

		salvar();

		categoria = '';
	}

	async function remover(id: number) {
		categorias = categorias.filter(c => c.id !== id);
		salvar();
	}

	async function salvar() {
		try {
			const categoriasData = await salvarCategorias(categorias);
			categorias.splice(0, categorias.length, ...categoriasData);
			notifications.success('Categorias salvas com sucesso!', 3000);
		} catch (error) {
			console.error('Erro ao salvar as categorias:', error);
			notifications.error('Erro ao salvar as categorias. Tente novamente.', 3000);
		}
	}
</script>

<SettingsCard collapsible={false}>
	<div class="space-y-6">
		<h1 class="text-2xl font-bold">Cadastro de categorias</h1>

		{#if categorias.length > 0}
			<table class="min-w-full bg-white rounded shadow">
				<thead>
					<tr>
						<th class="px-4 py-2 text-left">ID</th>
						<th class="px-4 py-2 text-left">Categoria</th>
						<th class="px-4 py-2"></th>
					</tr>
				</thead>
				<tbody>
					{#each categorias as categoria}
						<tr class="border-t">
							<td class="px-4 py-2">{categoria.id}</td>
							<td class="px-4 py-2">{categoria.categoria}</td>
							<td class="px-4 py-2">
								<button class="text-red-600 hover:text-red-800" on:click={() => remover(categoria.id)} aria-label="Remover categoria">
									<Trash />
								</button>
							</td>
						</tr>
					{/each}
				</tbody>
			</table>
		{:else}
			<p class="text-gray-500">Nenhuma categoria cadastrada.</p>
		{/if}

		<div class="bg-white p-6 rounded shadow max-w-xl">
			<label for="nome" class="block mb-2 font-medium">Nome da categoria</label>
			<input
				id="categoria"
				type="text"
				bind:value={categoria}
				class="w-full border rounded px-3 py-2 mb-4 focus:outline-none focus:ring-2 focus:ring-blue-500"
			/>

			<button
				on:click={adicionar}
				class="bg-blue-600 text-white px-4 py-2 rounded hover:bg-blue-700 transition"
			>
				Adicionar
			</button>
		</div>
	</div>
</SettingsCard>
