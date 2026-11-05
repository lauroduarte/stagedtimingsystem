<script lang="ts">
	import Trash from '~icons/tabler/trash';
	import Edit from '~icons/tabler/pencil';
	import Users from '~icons/tabler/users';
	import Search from '~icons/tabler/search';
	import SettingsCard from '$lib/components/SettingsCard.svelte';
	import { notifications } from '$lib/components/toasts/notifications';
	import { onDestroy, onMount } from 'svelte';
	import type { Atleta, Categoria, RFIDCard } from '$lib/types/models';
	import { getAtletas, getCategorias, getRFIDCards, salvarAtletas } from '$lib/services/services';
	import { socket } from '$lib/stores/socket';

	let atletas = $state<Atleta[]>([]);
	let editandoNumeral = $state<number | null>(null);
	let filtroBusca = $state('');

	let atleta = $state<Atleta>({
		nome: '',
		equipe: '',
		numeral: 1,
		categoriaId: 0
	});

	let categorias = $state<Categoria[]>([]);
	let rfidCards = $state<RFIDCard[]>([]);

	function obterCartaoPorNumeral(numeral: number): RFIDCard | undefined {
		return rfidCards.find((card) => card.numero === numeral);
	}

	function proximoNumeralDisponivel(): number {
		return atletas.length === 0 ? 1 : Math.max(0, ...atletas.map((a) => a.numeral)) + 1;
	}

	let atletasFiltrados = $derived.by(() => {
		const busca = filtroBusca.trim().toLowerCase();
		if (!busca) return atletas;
		return atletas.filter(
			(a) =>
				a.nome.toLowerCase().includes(busca) ||
				a.equipe.toLowerCase().includes(busca) ||
				String(a.numeral).includes(busca)
		);
	});

	function iniciarEdicao(item: Atleta) {
		editandoNumeral = item.numeral;
		atleta = {
			nome: item.nome,
			equipe: item.equipe,
			numeral: item.numeral,
			categoriaId: item.categoriaId
		};
	}

	function cancelarEdicao() {
		editandoNumeral = null;
		atleta = {
			nome: '',
			equipe: '',
			numeral: proximoNumeralDisponivel(),
			categoriaId: 0
		};
	}

	onMount(() => {
		getAtletas()
			.then((atletasData) => {
				atletas.splice(0, atletas.length, ...atletasData);
				atleta.numeral = proximoNumeralDisponivel();
			})
			.catch(() => {
				notifications.error('Erro ao carregar os atletas. Tente novamente.', 3000);
			});

		getCategorias()
			.then((categoriasData) => {
				categorias.splice(0, categorias.length, ...categoriasData);
			})
			.catch(() => {
				notifications.error('Erro ao carregar as categorias. Tente novamente.', 3000);
			});

		getRFIDCards()
			.then((cardsData) => {
				rfidCards.splice(0, rfidCards.length, ...cardsData);
			})
			.catch(() => {
				// Cartões podem ainda não ter sido inicializados
			});

		socket.on<any>('rfid', (data) => {
			if (data && Array.isArray(data.cards)) {
				rfidCards.splice(0, rfidCards.length, ...data.cards);
			}
		});
	});

	onDestroy(() => {
		socket.off('rfid');
	});

	async function remover(numeral: number) {
		if (editandoNumeral === numeral) {
			cancelarEdicao();
		}
		atletas = atletas.filter((item) => item.numeral !== numeral);
		await salvar();
	}

	async function salvarFormulario() {
		if (atleta.nome.trim() === '' || atleta.equipe.trim() === '' || atleta.numeral <= 0) {
			notifications.error('Todos os campos devem ser preenchidos corretamente.', 3000);
			return;
		}

		if (!atleta.categoriaId || atleta.categoriaId <= 0) {
			notifications.error('Selecione uma categoria válida para o atleta.', 3000);
			return;
		}

		if (
			atletas.some(
				(a) => a.numeral === atleta.numeral && a.numeral !== editandoNumeral
			)
		) {
			notifications.error('Numeral já existe.', 3000);
			return;
		}

		const registroAtleta: Atleta = {
			nome: atleta.nome.trim(),
			equipe: atleta.equipe.trim(),
			numeral: Number(atleta.numeral),
			categoriaId: Number(atleta.categoriaId)
		};

		if (editandoNumeral !== null) {
			const idx = atletas.findIndex((a) => a.numeral === editandoNumeral);
			if (idx !== -1) {
				atletas[idx] = registroAtleta;
			}
			await salvar();
			cancelarEdicao();
		} else {
			atletas.push(registroAtleta);
			await salvar();
			atleta = {
				nome: '',
				equipe: '',
				numeral: proximoNumeralDisponivel(),
				categoriaId: 0
			};
		}
	}

	async function salvar() {
		try {
			const atletasData = await salvarAtletas(atletas);
			atletas.splice(0, atletas.length, ...atletasData);
			notifications.success('Atletas salvos com sucesso!', 3000);
		} catch (error) {
			console.error('Erro ao salvar os atletas:', error);
			notifications.error('Erro ao salvar os atletas. Tente novamente.', 3000);
		}
	}
</script>

<SettingsCard collapsible={false} maxWidth="max-w-4xl">
	{#snippet icon()}
		<Users class="mr-2 inline h-6 w-6 text-primary shrink-0" />
	{/snippet}
	{#snippet title()}
		<span class="text-lg sm:text-xl font-bold">Pilotos & Atletas ({atletas.length})</span>
	{/snippet}

	<div class="space-y-4 sm:space-y-6 pt-1 w-full min-w-0">
		<!-- Tabela de Pilotos Cadastrados -->
		{#if atletas.length > 0}
			<div class="flex flex-col sm:flex-row sm:items-center justify-between gap-3">
				<p class="text-xs text-base-content/70">
					Relação de competidores cadastrados e seus numerais vinculados.
				</p>
				<div class="relative w-full sm:w-64">
					<Search class="absolute left-3 top-2.5 h-4 w-4 text-base-content/40" />
					<input
						type="text"
						placeholder="Buscar por nome, equipe ou #"
						bind:value={filtroBusca}
						class="input input-sm input-bordered w-full pl-9"
					/>
				</div>
			</div>

			<div class="overflow-x-auto w-full rounded-box border border-base-200 bg-base-100 shadow-sm">
				<table class="table table-zebra table-sm sm:table-md w-full">
					<thead>
						<tr class="bg-base-200/50">
							<th class="w-20 text-center">Numeral</th>
							<th>Nome do Piloto</th>
							<th>Equipe</th>
							<th>Categoria</th>
							<th>Cartão RFID</th>
							<th class="text-right w-20">Ações</th>
						</tr>
					</thead>
					<tbody>
						{#each atletasFiltrados as item (item.numeral)}
							{@const cartaoVinculado = obterCartaoPorNumeral(item.numeral)}
							<tr class={editandoNumeral === item.numeral ? 'bg-primary/10 font-semibold' : ''}>
								<td class="text-center font-mono">
									<span class="badge badge-primary badge-outline font-bold">
										#{item.numeral}
									</span>
								</td>
								<td class="font-medium text-sm whitespace-nowrap">{item.nome}</td>
								<td class="text-xs sm:text-sm text-base-content/80 whitespace-nowrap">{item.equipe}</td>
								<td class="text-xs whitespace-nowrap">
									{#if categorias.length > 0}
										{@const cat = categorias.find((c) => c.id === item.categoriaId)}
										<span class="badge badge-neutral badge-sm">
											{cat ? cat.categoria : 'Sem categoria'}
										</span>
									{:else}
										<span class="text-base-content/50">—</span>
									{/if}
								</td>
								<td class="text-xs whitespace-nowrap">
									{#if cartaoVinculado}
										<span class="badge badge-success badge-sm text-white font-mono gap-1">
											Cartão #{cartaoVinculado.numero} ({cartaoVinculado.uid})
										</span>
									{:else}
										<span class="badge badge-warning badge-outline badge-sm text-xs">
											Aguardando Reset #{item.numeral}
										</span>
									{/if}
								</td>
								<td class="text-right space-x-1 whitespace-nowrap">
									<button
										type="button"
										class="btn btn-ghost btn-circle btn-xs text-primary"
										onclick={() => iniciarEdicao(item)}
										aria-label="Editar atleta"
									>
										<Edit class="h-4 w-4" />
									</button>
									<button
										type="button"
										class="btn btn-ghost btn-circle btn-xs text-error"
										onclick={() => remover(item.numeral)}
										aria-label="Remover atleta"
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
				Nenhum atleta cadastrado. Utilize o formulário abaixo para registrar os pilotos da prova.
			</div>
		{/if}

		<!-- Formulário de Cadastro / Edição -->
		<div class="bg-base-100 p-4 sm:p-6 rounded-box border border-base-200 shadow-sm w-full">
			<h3 class="text-base font-bold mb-3 text-base-content flex items-center gap-2">
				<span>{editandoNumeral !== null ? `Editando Piloto #${editandoNumeral}` : 'Cadastrar Novo Piloto'}</span>
			</h3>

			<div class="grid grid-cols-1 sm:grid-cols-2 gap-3 mb-3">
				<div>
					<label for="nome" class="block mb-1 text-xs sm:text-sm font-semibold text-base-content">
						Nome do Atleta <span class="text-error">*</span>
					</label>
					<input
						id="nome"
						type="text"
						bind:value={atleta.nome}
						placeholder="Ex: Lucas Mendes"
						class="input input-bordered input-sm sm:input-md w-full text-sm"
					/>
				</div>

				<div>
					<label for="equipe" class="block mb-1 text-xs sm:text-sm font-semibold text-base-content">
						Equipe / Patrocinador <span class="text-error">*</span>
					</label>
					<input
						id="equipe"
						type="text"
						bind:value={atleta.equipe}
						placeholder="Ex: Racing Team"
						class="input input-bordered input-sm sm:input-md w-full text-sm"
					/>
				</div>
			</div>

			<div class="grid grid-cols-1 sm:grid-cols-2 gap-3 mb-4">
				<div>
					<label for="categoria" class="block mb-1 text-xs sm:text-sm font-semibold text-base-content">
						Categoria <span class="text-error">*</span>
					</label>
					<select
						id="categoria"
						bind:value={atleta.categoriaId}
						class="select select-bordered select-sm sm:select-md w-full text-sm"
					>
						<option value={0} disabled>Selecione uma categoria</option>
						{#each categorias as categoria}
							<option value={categoria.id}>{categoria.categoria}</option>
						{/each}
					</select>
				</div>

				<div>
					<label for="numeral" class="block mb-1 text-xs sm:text-sm font-semibold text-base-content">
						Numeral / ID do Chaveiro RFID <span class="text-error">*</span>
					</label>
					<input
						id="numeral"
						type="number"
						bind:value={atleta.numeral}
						min="1"
						class="input input-bordered input-sm sm:input-md w-full text-sm font-mono"
					/>
				</div>
			</div>

			<p class="text-xs text-base-content/60 mb-4">
				{#if obterCartaoPorNumeral(atleta.numeral)}
					Cartão RFID #{atleta.numeral} preparado: <strong class="font-mono">{obterCartaoPorNumeral(atleta.numeral)?.uid}</strong>
				{:else}
					Vinculado automaticamente ao Cartão RFID #{atleta.numeral} (prepare-o na tela de Reset).
				{/if}
			</p>

			<div class="pt-2 border-t border-base-200 flex flex-wrap items-center justify-end gap-2">
				{#if editandoNumeral !== null}
					<button
						type="button"
						onclick={cancelarEdicao}
						class="btn btn-ghost btn-sm sm:btn-md"
					>
						Cancelar
					</button>
				{/if}
				<button
					type="button"
					onclick={salvarFormulario}
					class="btn btn-primary btn-sm sm:btn-md min-w-28"
				>
					{editandoNumeral !== null ? 'Atualizar Atleta' : 'Salvar Atleta'}
				</button>
			</div>
		</div>
	</div>
</SettingsCard>
