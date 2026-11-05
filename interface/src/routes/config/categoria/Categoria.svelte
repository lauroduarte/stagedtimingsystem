<script lang="ts">
	import Trash from '~icons/tabler/trash';
	import Edit from '~icons/tabler/pencil';
	import UsersGroup from '~icons/tabler/users-group';
	import SettingsCard from '$lib/components/SettingsCard.svelte';
	import { notifications } from '$lib/components/toasts/notifications';
	import { onMount } from 'svelte';
	import type { Atleta, Categoria } from '$lib/types/models';
	import { getAtletas, getCategorias, salvarCategorias } from '$lib/services/services';

	let categorias = $state<Categoria[]>([]);
	let atletas = $state<Atleta[]>([]);

	let nextId = $derived(() => {
		return categorias.length > 0 ? Math.max(...categorias.map((c) => c.id)) + 1 : 1;
	});

	let categoria = $state('');
	let editandoId = $state<number | null>(null);

	onMount(async () => {
		try {
			const [categoriasData, atletasData] = await Promise.all([
				getCategorias(),
				getAtletas().catch(() => [])
			]);
			categorias.splice(0, categorias.length, ...categoriasData);
			atletas.splice(0, atletas.length, ...atletasData);
		} catch (error) {
			notifications.error('Erro ao carregar as categorias. Tente novamente.', 3000);
		}
	});

	function iniciarEdicao(item: Categoria) {
		editandoId = item.id;
		categoria = item.categoria;
	}

	function cancelarEdicao() {
		editandoId = null;
		categoria = '';
	}

	async function salvarFormulario() {
		const nomeLimpo = categoria.trim();
		if (nomeLimpo === '') {
			notifications.error('O nome da categoria não pode estar vazio.', 3000);
			return;
		}

		if (
			categorias.some(
				(c) => c.id !== editandoId && c.categoria.toLowerCase() === nomeLimpo.toLowerCase()
			)
		) {
			notifications.error('Categoria já existe.', 3000);
			return;
		}

		if (editandoId !== null) {
			const idx = categorias.findIndex((c) => c.id === editandoId);
			if (idx !== -1) {
				categorias[idx] = {
					id: editandoId,
					categoria: nomeLimpo
				};
			}
			await salvar();
			cancelarEdicao();
		} else {
			categorias.push({
				id: nextId(),
				categoria: nomeLimpo
			});
			await salvar();
			categoria = '';
		}
	}

	async function remover(id: number) {
		try {
			const atletasAtuais = await getAtletas().catch(() => atletas);
			atletas.splice(0, atletas.length, ...atletasAtuais);
		} catch {
			// Usa lista em memória caso ocorra falha momentânea
		}

		if (atletas.some((a) => a.categoriaId === id)) {
			notifications.error(
				'Não é possível remover esta categoria pois há atletas vinculados a ela.',
				4000
			);
			return;
		}

		if (editandoId === id) {
			cancelarEdicao();
		}

		categorias = categorias.filter((c) => c.id !== id);
		await salvar();
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

<SettingsCard collapsible={false} maxWidth="max-w-3xl">
	{#snippet icon()}
		<UsersGroup class="mr-2 inline h-6 w-6 text-primary shrink-0" />
	{/snippet}
	{#snippet title()}
		<span class="text-lg sm:text-xl font-bold">Categorias ({categorias.length})</span>
	{/snippet}

	<div class="space-y-4 sm:space-y-6 pt-1 w-full min-w-0">
		{#if categorias.length > 0}
			<div class="overflow-x-auto w-full rounded-box border border-base-200 bg-base-100 shadow-sm">
				<table class="table table-zebra table-sm sm:table-md w-full">
					<thead>
						<tr class="bg-base-200/50">
							<th class="w-16">ID</th>
							<th>Categoria</th>
							<th class="text-right w-24">Ações</th>
						</tr>
					</thead>
					<tbody>
						{#each categorias as item (item.id)}
							<tr class={editandoId === item.id ? 'bg-primary/10 font-semibold' : ''}>
								<td class="font-mono text-xs">{item.id}</td>
								<td class="font-medium text-sm">{item.categoria}</td>
								<td class="text-right space-x-1 whitespace-nowrap">
									<button
										type="button"
										class="btn btn-ghost btn-circle btn-xs text-primary"
										onclick={() => iniciarEdicao(item)}
										aria-label="Editar categoria"
									>
										<Edit class="h-4 w-4" />
									</button>
									<button
										type="button"
										class="btn btn-ghost btn-circle btn-xs text-error"
										onclick={() => remover(item.id)}
										aria-label="Remover categoria"
									>
										<Trash class="h-4 w-4" />
									</button>
								</td>
							</tr>
						{/each}
					</tbody>
				</table>
			</div>
		{:else}
			<div class="p-6 text-center text-sm text-base-content/60 bg-base-100 rounded-box border border-base-200">
				Nenhuma categoria cadastrada. Adicione abaixo a primeira categoria da prova.
			</div>
		{/if}

		<div class="bg-base-100 p-4 sm:p-6 rounded-box border border-base-200 shadow-sm w-full">
			<label for="categoria" class="block mb-1.5 text-xs sm:text-sm font-semibold text-base-content">
				{editandoId !== null ? `Editando categoria #${editandoId}` : 'Nome da Nova Categoria'}
			</label>
			<div class="flex flex-col sm:flex-row gap-2">
				<input
					id="categoria"
					type="text"
					bind:value={categoria}
					placeholder="Ex: Elite Masculino, Master 30, Open"
					class="input input-bordered input-sm sm:input-md flex-1 text-sm"
					onkeydown={(e) => { if (e.key === 'Enter') salvarFormulario(); }}
				/>

				<div class="flex items-center gap-2 shrink-0">
					<button
						type="button"
						onclick={salvarFormulario}
						class="btn btn-primary btn-sm sm:btn-md flex-1 sm:flex-initial"
					>
						{editandoId !== null ? 'Atualizar' : 'Adicionar'}
					</button>
					{#if editandoId !== null}
						<button
							type="button"
							onclick={cancelarEdicao}
							class="btn btn-ghost btn-sm sm:btn-md"
						>
							Cancelar
						</button>
					{/if}
				</div>
			</div>
		</div>
	</div>
</SettingsCard>
