<script lang="ts">
	import SettingsCard from '$lib/components/SettingsCard.svelte';
	import { notifications } from '$lib/components/toasts/notifications';
	import { onDestroy, onMount } from 'svelte';
	import type { Atleta, Categoria, CronometragemItem, Prova, RFIDCard } from '$lib/types/models';
	import {
		getAtletas,
		getCategorias,
		getCronometragens,
		getProva,
		getRFIDCards,
		getSyncResultadosLocal,
		resetarDadosEstacao,
		startRFIDReset,
		stopRFIDReset
	} from '$lib/services/services';
	import { socket } from '$lib/stores/socket';
	import Eraser from '~icons/tabler/eraser';
	import PlayerPlay from '~icons/tabler/player-play';
	import PlayerStop from '~icons/tabler/player-stop';
	import Check from '~icons/tabler/check';
	import AlertCircle from '~icons/tabler/alert-circle';
	import AlertTriangle from '~icons/tabler/alert-triangle';
	import Search from '~icons/tabler/search';
	import Refresh from '~icons/tabler/refresh';
	import Id from '~icons/tabler/id';
	import Help from '~icons/tabler/help';
	import Trash from '~icons/tabler/trash';
	import Download from '~icons/tabler/download';
	import FileSpreadsheet from '~icons/tabler/file-spreadsheet';
	import Flag from '~icons/tabler/flag';
	import Users from '~icons/tabler/users';
	import Stopwatch from '~icons/tabler/stopwatch';
	import CheckboxIcon from '~icons/tabler/square-check';
	import SquareIcon from '~icons/tabler/square';

	type AbaAtiva = 'estacao' | 'rfid';
	let abaAtiva = $state<AbaAtiva>('estacao');

	// Dados da Estação
	let prova = $state<Prova | undefined>();
	let categorias = $state<Categoria[]>([]);
	let atletas = $state<Atleta[]>([]);
	let cronometragens = $state<CronometragemItem[]>([]);
	let rfidCards = $state<RFIDCard[]>([]);

	// Estados de controle RFID
	let emReset = $state(false);
	let carregando = $state(true);
	let processandoAcao = $state(false);
	let filtroBusca = $state('');
	let ultimoChaveiro = $state<{ numero: number; uid: string; hora: string } | null>(null);

	// Opções do Reset da Estação (Checkboxes selecionados por padrão)
	let optLimparTempos = $state(true);
	let optLimparProva = $state(true);
	let optLimparAtletas = $state(true);
	let optLimparCategorias = $state(true);
	let optLimparRfidCards = $state(true);
	let modalConfirmacaoAberto = $state(false);
	let resetandoEstacao = $state(false);

	let totalSelecionados = $derived.by(() => {
		let count = 0;
		if (optLimparTempos) count++;
		if (optLimparProva) count++;
		if (optLimparAtletas) count++;
		if (optLimparCategorias) count++;
		if (optLimparRfidCards) count++;
		return count;
	});

	function selecionarTodos() {
		optLimparTempos = true;
		optLimparProva = true;
		optLimparAtletas = true;
		optLimparCategorias = true;
		optLimparRfidCards = true;
	}

	function desmarcarTodos() {
		optLimparTempos = false;
		optLimparProva = false;
		optLimparAtletas = false;
		optLimparCategorias = false;
		optLimparRfidCards = false;
	}

	let proximoNumero = $derived.by(() => {
		if (rfidCards.length === 0) return 1;
		const max = Math.max(...rfidCards.map((c) => c.numero || 0));
		return max + 1;
	});

	let cartoesFiltrados = $derived.by(() => {
		const busca = filtroBusca.trim().toLowerCase();
		const lista = [...rfidCards].sort((a, b) => (b.numero || 0) - (a.numero || 0));
		if (!busca) return lista;
		return lista.filter(
			(card) => card.uid.toLowerCase().includes(busca) || String(card.numero).includes(busca)
		);
	});

	onMount(async () => {
		await recarregarTudo();

		socket.on<any>('rfid', (data) => {
			if (data && Array.isArray(data.cards)) {
				const qtdAnterior = rfidCards.length;
				rfidCards = [...data.cards];

				// Detecta o último cartão adicionado
				if (rfidCards.length > 0) {
					const maisRecente = [...rfidCards].sort((a, b) => (b.numero || 0) - (a.numero || 0))[0];
					if (maisRecente) {
						ultimoChaveiro = {
							numero: maisRecente.numero,
							uid: maisRecente.uid,
							hora: new Date().toLocaleTimeString('pt-BR')
						};

						if (rfidCards.length > qtdAnterior) {
							notifications.success(
								`Chaveiro #${maisRecente.numero} (${maisRecente.uid}) resetado e preparado com sucesso!`,
								3500
							);
						}
					}
				}
			}
		});
	});

	onDestroy(() => {
		socket.off('rfid');
	});

	async function recarregarTudo() {
		carregando = true;
		try {
			const [provaData, categoriasData, atletasData, cardsData, cronoData] = await Promise.all([
				getProva().catch(() => undefined),
				getCategorias().catch(() => []),
				getAtletas().catch(() => []),
				getRFIDCards().catch(() => []),
				getCronometragens().catch(() => [])
			]);

			prova = provaData;
			categorias = Array.isArray(categoriasData) ? categoriasData : [];
			atletas = Array.isArray(atletasData) ? atletasData : [];
			rfidCards = Array.isArray(cardsData) ? cardsData : [];
			cronometragens = Array.isArray(cronoData) ? cronoData : [];

			if (rfidCards.length > 0 && !ultimoChaveiro) {
				const maisRecente = [...rfidCards].sort((a, b) => (b.numero || 0) - (a.numero || 0))[0];
				ultimoChaveiro = {
					numero: maisRecente.numero,
					uid: maisRecente.uid,
					hora: 'Sessão anterior'
				};
			}
		} catch (error) {
			notifications.error('Erro ao carregar dados da estação.', 3000);
		} finally {
			carregando = false;
		}
	}

	async function handleExecutarResetEstacao() {
		if (resetandoEstacao) return;

		if (totalSelecionados === 0) {
			notifications.warning('Selecione pelo menos um item para resetar.', 3000);
			return;
		}

		resetandoEstacao = true;
		try {
			const resp = await resetarDadosEstacao({
				limparCronometragem: optLimparTempos,
				limparProva: optLimparProva,
				limparAtletas: optLimparAtletas,
				limparCategorias: optLimparCategorias,
				limparRfidCards: optLimparRfidCards,
				pararCronometragem: true
			});

			modalConfirmacaoAberto = false;
			await recarregarTudo();
			ultimoChaveiro = null;

			notifications.success(
				resp.mensagem || 'Estação resetada com sucesso! Você pode iniciar uma nova prova.',
				5000
			);
		} catch (error: any) {
			notifications.error(error?.message || 'Falha ao resetar os dados da estação.', 4000);
		} finally {
			resetandoEstacao = false;
		}
	}

	async function baixarBackupJson() {
		try {
			const pacote = await getSyncResultadosLocal().catch(() => ({
				cronometragem: cronometragens
			}));
			const dataStr =
				'data:text/json;charset=utf-8,' +
				encodeURIComponent(
					JSON.stringify(
						{
							exportado_em: new Date().toISOString(),
							prova,
							atletas,
							categorias,
							rfidCards,
							cronometragem: pacote.cronometragem || cronometragens
						},
						null,
						2
					)
				);
			const link = document.createElement('a');
			link.setAttribute('href', dataStr);
			link.setAttribute('download', `backup_estacao_${new Date().toISOString().slice(0, 10)}.json`);
			document.body.appendChild(link);
			link.click();
			link.remove();
			notifications.success('Backup JSON baixado com sucesso!', 3000);
		} catch {
			notifications.error('Falha ao gerar backup JSON.', 3000);
		}
	}

	async function baixarBackupCsv() {
		try {
			const headers = [
				'Numeral',
				'Piloto',
				'Equipe',
				'UID_Cartao',
				'Etapa_Volta',
				'Inicio',
				'Fim',
				'Duracao',
				'Status'
			];
			const mapaAtletas = new Map(atletas.map((a) => [a.numeral, a]));

			const rows = cronometragens.map((item) => {
				const num = item.numeral || 0;
				const atl = num > 0 ? mapaAtletas.get(num) : undefined;
				return [
					num > 0 ? String(num) : '',
					atl?.nome || '',
					atl?.equipe || '',
					item.uid || '',
					item.especial || '',
					item.inicio || '',
					item.fim || '',
					item.duracao || '',
					item.status || ''
				].map((v) => `"${String(v).replace(/"/g, '""')}"`);
			});

			const texto = [headers.join(';'), ...rows.map((r) => r.join(';'))].join('\r\n');
			const blob = new Blob(['\uFEFF' + texto], { type: 'text/csv;charset=utf-8;' });
			const url = URL.createObjectURL(blob);
			const link = document.createElement('a');
			link.setAttribute('href', url);
			link.setAttribute(
				'download',
				`backup_passagens_${new Date().toISOString().slice(0, 10)}.csv`
			);
			document.body.appendChild(link);
			link.click();
			link.remove();
			URL.revokeObjectURL(url);
			notifications.success('Relatório CSV baixado com sucesso!', 3000);
		} catch {
			notifications.error('Falha ao gerar CSV.', 3000);
		}
	}

	async function handleStartReset() {
		if (processandoAcao) return;
		if (
			rfidCards.length > 0 &&
			!confirm(
				'Atenção: Iniciar uma nova sessão de reset de chaveiros reiniciará a contagem sequencial dos chaveiros a partir do #1. Deseja continuar?'
			)
		) {
			return;
		}

		processandoAcao = true;
		try {
			await startRFIDReset();
			rfidCards = [];
			ultimoChaveiro = null;
			emReset = true;
			notifications.success('Modo Reset ativado! Aproxime os chaveiros do leitor.', 4000);
		} catch (error) {
			notifications.error('Falha ao ativar o modo Reset no ESP32.', 3000);
		} finally {
			processandoAcao = false;
		}
	}

	async function handleStopReset() {
		if (processandoAcao) return;
		processandoAcao = true;
		try {
			const cards = await stopRFIDReset();
			if (Array.isArray(cards)) {
				rfidCards = [...cards];
			}
			emReset = false;
			notifications.success('Modo Reset encerrado com sucesso.', 3000);
		} catch (error) {
			notifications.error('Erro ao encerrar o modo de reset.', 3000);
		} finally {
			processandoAcao = false;
		}
	}
</script>

<SettingsCard collapsible={false} maxWidth="max-w-5xl">
	{#snippet icon()}
		<Eraser class="mr-2 inline h-6 w-6 text-primary shrink-0" />
	{/snippet}
	{#snippet title()}
		<span class="text-lg sm:text-xl font-bold">Reset da Estação & Chaveiros RFID</span>
	{/snippet}

	<div class="space-y-4 sm:space-y-6 pt-1 w-full min-w-0">
		<!-- Navegação por Abas (Segmented Control) Responsivo -->
		<div class="tabs tabs-boxed bg-base-300/80 p-1 rounded-box grid grid-cols-2 gap-1 w-full">
			<button
				type="button"
				class="tab text-xs sm:text-sm font-bold gap-1 sm:gap-2 h-10 transition-all {abaAtiva ===
				'estacao'
					? 'tab-active bg-primary text-primary-content shadow'
					: 'text-base-content/80 hover:text-base-content'}"
				onclick={() => (abaAtiva = 'estacao')}
			>
				<Trash class="h-4 w-4 shrink-0" />
				<span class="truncate">Reset da Estação (Nova Prova)</span>
			</button>
			<button
				type="button"
				class="tab text-xs sm:text-sm font-bold gap-1 sm:gap-2 h-10 transition-all {abaAtiva ===
				'rfid'
					? 'tab-active bg-primary text-primary-content shadow'
					: 'text-base-content/80 hover:text-base-content'}"
				onclick={() => (abaAtiva = 'rfid')}
			>
				<Id class="h-4 w-4 shrink-0" />
				<span class="truncate">Preparar Chaveiros RFID</span>
			</button>
		</div>

		{#if abaAtiva === 'estacao'}
			<!-- ============================================================= -->
			<!-- ABA 1: RESET DA ESTAÇÃO COM CHECKBOXES DO QUE SERÁ RESETADO  -->
			<!-- ============================================================= -->
			<div class="space-y-4 sm:space-y-6">
				<!-- Card Principal da Lista com Checkboxes -->
				<div class="card bg-base-100 border-2 border-base-300 shadow-md">
					<div class="card-body p-3.5 sm:p-6 space-y-4">
						<!-- Cabeçalho da Lista de Reset -->
						<div
							class="flex flex-col sm:flex-row sm:items-center justify-between gap-3 pb-3 border-b border-base-200"
						>
							<div>
								<div class="flex items-center gap-2">
									<Trash class="h-5 w-5 text-error shrink-0" />
									<h3 class="text-base sm:text-lg font-bold text-base-content">
										O que você deseja resetar?
									</h3>
								</div>
								<p class="text-xs sm:text-sm text-base-content/70 mt-1">
									Marque com checkbox os itens que devem ser apagados da memória para a nova prova:
								</p>
							</div>

							<!-- Barra com Botões de Selecionar Todos / Desmarcar Todos -->
							<div class="flex items-center gap-2 self-start sm:self-center flex-wrap">
								<button
									type="button"
									onclick={selecionarTodos}
									class="btn btn-outline btn-xs sm:btn-sm gap-1"
									title="Marcar todos os checkboxes"
								>
									<CheckboxIcon class="h-3.5 w-3.5 text-primary" />
									<span>Marcar Todos</span>
								</button>
								<button
									type="button"
									onclick={desmarcarTodos}
									class="btn btn-outline btn-xs sm:btn-sm gap-1"
									title="Desmarcar todos os checkboxes"
								>
									<SquareIcon class="h-3.5 w-3.5 text-base-content/60" />
									<span>Desmarcar Todos</span>
								</button>
								<button
									type="button"
									onclick={recarregarTudo}
									disabled={carregando}
									class="btn btn-ghost btn-circle btn-xs sm:btn-sm"
									title="Atualizar dados atuais"
								>
									<Refresh class="h-4 w-4 {carregando ? 'animate-spin' : ''}" />
								</button>
							</div>
						</div>

						<!-- Contador de Itens Selecionados -->
						<div
							class="flex items-center justify-between text-xs font-semibold px-1 text-base-content/80"
						>
							<span>
								Itens selecionados para exclusão:
								<span
									class="badge {totalSelecionados > 0
										? 'badge-error text-white font-bold'
										: 'badge-ghost'} badge-sm ml-1"
								>
									{totalSelecionados} de 5 selecionados
								</span>
							</span>
							{#if totalSelecionados === 0}
								<span class="text-warning text-xs">Nenhum item marcado</span>
							{/if}
						</div>

						<!-- Lista Detalhada com Checkboxes Grandes e Claros -->
						<div class="space-y-2.5">
							<!-- 1. Checkbox: Tempos e Passagens de Cronometragem -->
							<label
								class="flex items-start gap-3 p-3 sm:p-4 rounded-xl border-2 transition-all cursor-pointer {optLimparTempos
									? 'border-error/60 bg-error/5 shadow-xs'
									: 'border-base-200 bg-base-100 hover:bg-base-200/40 opacity-75'}"
							>
								<input
									type="checkbox"
									bind:checked={optLimparTempos}
									class="checkbox checkbox-error checkbox-md mt-0.5 shrink-0"
								/>
								<div class="flex-1 min-w-0">
									<div class="flex flex-wrap items-center justify-between gap-1.5">
										<span class="font-bold text-sm sm:text-base text-base-content">
											1. Passagens e Tempos da Cronometragem
										</span>
										<span class="badge badge-error badge-outline badge-sm font-semibold">
											{cronometragens.length} registros
										</span>
									</div>
									<p class="text-xs text-base-content/70 mt-1">
										Apaga todos os registros de largadas, chegadas, parciais e tempos gravados nesta
										estação durante as baterias.
									</p>
								</div>
							</label>

							<!-- 2. Checkbox: Dados Cadastrais da Prova -->
							<label
								class="flex items-start gap-3 p-3 sm:p-4 rounded-xl border-2 transition-all cursor-pointer {optLimparProva
									? 'border-error/60 bg-error/5 shadow-xs'
									: 'border-base-200 bg-base-100 hover:bg-base-200/40 opacity-75'}"
							>
								<input
									type="checkbox"
									bind:checked={optLimparProva}
									class="checkbox checkbox-error checkbox-md mt-0.5 shrink-0"
								/>
								<div class="flex-1 min-w-0">
									<div class="flex flex-wrap items-center justify-between gap-1.5">
										<span class="font-bold text-sm sm:text-base text-base-content">
											2. Dados Cadastrais da Prova
										</span>
										<span
											class="badge badge-warning badge-outline badge-sm font-semibold truncate max-w-[180px]"
										>
											{prova?.nome || 'Não cadastrada'}
										</span>
									</div>
									<p class="text-xs text-base-content/70 mt-1">
										Limpa o nome da prova atual, data, local e retorna os parâmetros de
										percurso/estágio aos padrões de fábrica.
									</p>
								</div>
							</label>

							<!-- 3. Checkbox: Pilotos / Atletas -->
							<label
								class="flex items-start gap-3 p-3 sm:p-4 rounded-xl border-2 transition-all cursor-pointer {optLimparAtletas
									? 'border-error/60 bg-error/5 shadow-xs'
									: 'border-base-200 bg-base-100 hover:bg-base-200/40 opacity-75'}"
							>
								<input
									type="checkbox"
									bind:checked={optLimparAtletas}
									class="checkbox checkbox-error checkbox-md mt-0.5 shrink-0"
								/>
								<div class="flex-1 min-w-0">
									<div class="flex flex-wrap items-center justify-between gap-1.5">
										<span class="font-bold text-sm sm:text-base text-base-content">
											3. Pilotos e Atletas Cadastrados
										</span>
										<span class="badge badge-info badge-outline badge-sm font-semibold">
											{atletas.length} atletas
										</span>
									</div>
									<p class="text-xs text-base-content/70 mt-1">
										Remove toda a lista de pilotos, seus números de numeral e vínculos com chips
										para receber os competidores da nova prova.
									</p>
								</div>
							</label>

							<!-- 4. Checkbox: Categorias -->
							<label
								class="flex items-start gap-3 p-3 sm:p-4 rounded-xl border-2 transition-all cursor-pointer {optLimparCategorias
									? 'border-error/60 bg-error/5 shadow-xs'
									: 'border-base-200 bg-base-100 hover:bg-base-200/40 opacity-75'}"
							>
								<input
									type="checkbox"
									bind:checked={optLimparCategorias}
									class="checkbox checkbox-error checkbox-md mt-0.5 shrink-0"
								/>
								<div class="flex-1 min-w-0">
									<div class="flex flex-wrap items-center justify-between gap-1.5">
										<span class="font-bold text-sm sm:text-base text-base-content">
											4. Categorias da Prova
										</span>
										<span class="badge badge-secondary badge-outline badge-sm font-semibold">
											{categorias.length} categorias
										</span>
									</div>
									<p class="text-xs text-base-content/70 mt-1">
										Apaga as categorias da prova anterior para você cadastrar as divisões do novo
										evento.
									</p>
								</div>
							</label>

							<!-- 5. Checkbox: Chaveiros RFID -->
							<label
								class="flex items-start gap-3 p-3 sm:p-4 rounded-xl border-2 transition-all cursor-pointer {optLimparRfidCards
									? 'border-error/60 bg-error/5 shadow-xs'
									: 'border-base-200 bg-base-100 hover:bg-base-200/40 opacity-75'}"
							>
								<input
									type="checkbox"
									bind:checked={optLimparRfidCards}
									class="checkbox checkbox-error checkbox-md mt-0.5 shrink-0"
								/>
								<div class="flex-1 min-w-0">
									<div class="flex flex-wrap items-center justify-between gap-1.5">
										<span class="font-bold text-sm sm:text-base text-base-content">
											5. Memória de Chaveiros RFID Lidos
										</span>
										<span class="badge badge-primary badge-outline badge-sm font-semibold">
											{rfidCards.length} chaveiros gravados
										</span>
									</div>
									<p class="text-xs text-base-content/70 mt-1">
										Limpa a lista de chips/tags gravadas na estação e reinicia o contador de
										numeração sequencial a partir do #1.
									</p>
								</div>
							</label>
						</div>

						<!-- Seção de Backup Preventivo -->
						<div class="p-3 sm:p-4 rounded-xl bg-base-200/70 border border-base-300">
							<div class="flex flex-col sm:flex-row sm:items-center justify-between gap-3">
								<div>
									<strong class="text-xs sm:text-sm text-base-content block"
										>Backup de Segurança (Opcional):</strong
									>
									<span class="text-xs text-base-content/70">
										Faça o download dos dados atuais antes de apagar a memória da estação.
									</span>
								</div>
								<div class="flex flex-wrap items-center gap-2">
									<button
										type="button"
										onclick={baixarBackupJson}
										class="btn btn-outline btn-xs sm:btn-sm gap-1"
									>
										<Download class="h-3.5 w-3.5" />
										<span>Backup JSON</span>
									</button>
									<button
										type="button"
										onclick={baixarBackupCsv}
										class="btn btn-outline btn-xs sm:btn-sm gap-1"
									>
										<FileSpreadsheet class="h-3.5 w-3.5" />
										<span>Planilha CSV</span>
									</button>
								</div>
							</div>
						</div>

						<!-- Barra de Ação de Reset -->
						<div
							class="pt-2 flex flex-col sm:flex-row sm:items-center justify-between gap-3 border-t border-base-200"
						>
							<div class="text-xs text-base-content/70">
								{#if totalSelecionados === 0}
									<span class="text-warning font-medium"
										>Selecione ao menos um item acima para habilitar o reset.</span
									>
								{:else}
									<span class="text-error font-medium"
										>Os {totalSelecionados} itens marcados serão apagados permanentemente.</span
									>
								{/if}
							</div>

							<button
								type="button"
								onclick={() => (modalConfirmacaoAberto = true)}
								disabled={totalSelecionados === 0 || resetandoEstacao}
								class="btn btn-error text-white btn-md gap-2 shadow-md w-full sm:w-auto"
							>
								{#if resetandoEstacao}
									<span class="loading loading-spinner loading-sm"></span>
									<span>Resetando...</span>
								{:else}
									<Trash class="h-5 w-5" />
									<span>Resetar Itens Selecionados ({totalSelecionados})</span>
								{/if}
							</button>
						</div>
					</div>
				</div>
			</div>
		{:else}
			<!-- ============================================================= -->
			<!-- ABA 2: PREPARAÇÃO E RESET DE CHAVEIROS RFID MFRC522           -->
			<!-- ============================================================= -->
			<div class="space-y-4 sm:space-y-6">
				<!-- Painel de Status do Modo Reset RFID -->
				<div
					class="rounded-box p-4 sm:p-5 border transition-all {emReset
						? 'bg-primary/10 border-primary shadow-lg shadow-primary/10'
						: 'bg-base-100 border-base-300'}"
				>
					<div class="flex flex-col sm:flex-row sm:items-center justify-between gap-4">
						<div class="flex items-center gap-3 min-w-0">
							<div
								class="flex h-11 w-11 sm:h-12 sm:w-12 items-center justify-center rounded-full shrink-0 {emReset
									? 'bg-primary text-primary-content animate-pulse'
									: 'bg-base-200 text-base-content/60'}"
							>
								{#if emReset}
									<Refresh class="h-5 w-5 sm:h-6 sm:w-6 animate-spin" />
								{:else}
									<Id class="h-5 w-5 sm:h-6 sm:w-6" />
								{/if}
							</div>
							<div class="min-w-0">
								<div class="flex items-center gap-2 flex-wrap">
									<h3 class="text-base sm:text-lg font-bold text-base-content">
										Preparação de Chaveiros RFID Mifare
									</h3>
									{#if emReset}
										<span class="badge badge-primary font-bold text-xs animate-pulse"
											>AGUARDANDO CHAVEIRO</span
										>
									{:else}
										<span class="badge badge-ghost text-xs">MODO ESPERA</span>
									{/if}
								</div>
								<p class="text-xs sm:text-sm text-base-content/70 mt-0.5">
									{emReset
										? 'Aproxime os chaveiros no leitor RC522. Cada aproximação grava o número sequencial.'
										: 'Inicie a sessão para limpar e numerar os chaveiros físicos Mifare Classic.'}
								</p>
							</div>
						</div>

						<div class="flex items-center gap-2 self-stretch sm:self-center">
							{#if !emReset}
								<button
									type="button"
									class="btn btn-primary text-primary-content btn-sm sm:btn-md gap-2 flex-1 sm:flex-initial"
									disabled={processandoAcao}
									onclick={handleStartReset}
								>
									{#if processandoAcao}
										<span class="loading loading-spinner loading-xs"></span>
									{:else}
										<PlayerPlay class="h-4 w-4" />
									{/if}
									<span>Iniciar Preparação</span>
								</button>
							{:else}
								<button
									type="button"
									class="btn btn-error text-white btn-sm sm:btn-md gap-2 flex-1 sm:flex-initial"
									disabled={processandoAcao}
									onclick={handleStopReset}
								>
									{#if processandoAcao}
										<span class="loading loading-spinner loading-xs"></span>
									{:else}
										<PlayerStop class="h-4 w-4" />
									{/if}
									<span>Parar Preparação</span>
								</button>
							{/if}
						</div>
					</div>
				</div>

				<!-- Métricas Rápidas dos Chaveiros -->
				<div class="grid grid-cols-2 sm:grid-cols-3 gap-2 sm:gap-4">
					<div class="p-3 sm:p-4 rounded-box bg-base-100 border border-base-200">
						<div class="text-xs text-base-content/60">Total Preparados</div>
						<div class="text-xl sm:text-2xl font-bold text-primary mt-1">{rfidCards.length}</div>
						<div class="text-[11px] text-base-content/60 mt-0.5">Na sessão atual</div>
					</div>
					<div class="p-3 sm:p-4 rounded-box bg-base-100 border border-base-200">
						<div class="text-xs text-base-content/60">Próximo Número</div>
						<div class="text-xl sm:text-2xl font-bold text-secondary mt-1">#{proximoNumero}</div>
						<div class="text-[11px] text-base-content/60 mt-0.5">Próxima tag lida</div>
					</div>
					<div
						class="p-3 sm:p-4 rounded-box bg-base-100 border border-base-200 col-span-2 sm:col-span-1"
					>
						<div class="text-xs text-base-content/60">Último Lido</div>
						{#if ultimoChaveiro}
							<div class="text-base sm:text-lg font-bold text-success truncate mt-1">
								#{ultimoChaveiro.numero}
								<span class="text-xs font-mono font-normal">({ultimoChaveiro.uid})</span>
							</div>
							<div class="text-[11px] text-base-content/60 mt-0.5">{ultimoChaveiro.hora}</div>
						{:else}
							<div class="text-sm font-medium text-base-content/50 mt-1">Nenhum lido ainda</div>
							<div class="text-[11px] text-base-content/60 mt-0.5">Aguardando aproximação</div>
						{/if}
					</div>
				</div>

				<!-- Tabela de Chaveiros Preparados com Overflow Responsivo -->
				<div class="card bg-base-100 border border-base-300 shadow-sm">
					<div class="card-body p-3 sm:p-5 space-y-3">
						<div class="flex flex-col sm:flex-row sm:items-center justify-between gap-2">
							<h4
								class="font-bold text-sm sm:text-base text-base-content flex items-center gap-1.5"
							>
								<Id class="h-4 w-4 text-primary" />
								<span>Chaveiros Gravados ({rfidCards.length})</span>
							</h4>

							{#if rfidCards.length > 5}
								<div class="relative w-full sm:w-64">
									<Search class="h-4 w-4 absolute left-3 top-2.5 text-base-content/50" />
									<input
										type="text"
										bind:value={filtroBusca}
										placeholder="Buscar UID ou #"
										class="input input-sm input-bordered w-full pl-9"
									/>
								</div>
							{/if}
						</div>

						{#if rfidCards.length === 0}
							<div class="text-center py-8 text-base-content/60 text-xs sm:text-sm">
								Nenhum chaveiro gravado nesta sessão. Clique em <strong>"Iniciar Preparação"</strong
								> e aproxime os chaveiros do leitor RFID.
							</div>
						{:else}
							<div class="overflow-x-auto w-full rounded-lg border border-base-200">
								<table class="table table-sm sm:table-md w-full">
									<thead>
										<tr class="bg-base-200/60 text-xs">
											<th class="w-20">Número</th>
											<th>UID da Tag</th>
											<th class="text-right">Status</th>
										</tr>
									</thead>
									<tbody>
										{#each cartoesFiltrados as card (card.uid)}
											<tr class="hover">
												<td class="font-bold text-primary">#{card.numero}</td>
												<td class="font-mono text-xs">{card.uid}</td>
												<td class="text-right">
													<span class="badge badge-success badge-xs sm:badge-sm gap-1">
														<Check class="h-3 w-3" />
														<span>Gravado</span>
													</span>
												</td>
											</tr>
										{/each}
									</tbody>
								</table>
							</div>
						{/if}
					</div>
				</div>
			</div>
		{/if}
	</div>
</SettingsCard>

<!-- Modal de Confirmação Segura para Reset da Estação -->
{#if modalConfirmacaoAberto}
	<div class="modal modal-open z-50">
		<div class="modal-box w-11/12 max-w-lg border border-error/30 shadow-2xl">
			<div class="flex items-start gap-3">
				<div class="p-3 rounded-full bg-error/15 text-error shrink-0">
					<AlertTriangle class="h-7 w-7" />
				</div>
				<div>
					<h3 class="text-lg font-bold text-base-content">Confirmar Reset dos Dados?</h3>
					<p class="text-xs text-base-content/70 mt-1">
						Os itens selecionados abaixo serão apagados da memória permanente da estação. Esta
						operação é irreversível.
					</p>
				</div>
			</div>

			<div class="my-4 p-3 rounded-box bg-base-200 text-xs space-y-1 text-base-content/80">
				<strong class="text-base-content block mb-1"
					>Itens que serão apagados ({totalSelecionados}):</strong
				>
				{#if optLimparTempos}
					<div class="flex items-center gap-1.5 text-error font-medium">
						<Trash class="h-3.5 w-3.5 shrink-0" />
						<span>{cronometragens.length} registro(s) de tempos/passagens</span>
					</div>
				{/if}
				{#if optLimparProva}
					<div class="flex items-center gap-1.5 text-error font-medium">
						<Flag class="h-3.5 w-3.5 shrink-0" />
						<span>Dados cadastrais da prova ({prova?.nome || 'sem nome'})</span>
					</div>
				{/if}
				{#if optLimparAtletas}
					<div class="flex items-center gap-1.5 text-error font-medium">
						<Users class="h-3.5 w-3.5 shrink-0" />
						<span>{atletas.length} piloto(s) / atleta(s) cadastrados</span>
					</div>
				{/if}
				{#if optLimparCategorias}
					<div class="flex items-center gap-1.5 text-error font-medium">
						<Trash class="h-3.5 w-3.5 shrink-0" />
						<span>{categorias.length} categoria(s)</span>
					</div>
				{/if}
				{#if optLimparRfidCards}
					<div class="flex items-center gap-1.5 text-error font-medium">
						<Id class="h-3.5 w-3.5 shrink-0" />
						<span>{rfidCards.length} chaveiro(s) RFID (próximo volta para #1)</span>
					</div>
				{/if}
			</div>

			<div class="modal-action flex items-center justify-end gap-2">
				<button
					type="button"
					onclick={() => (modalConfirmacaoAberto = false)}
					disabled={resetandoEstacao}
					class="btn btn-ghost btn-sm sm:btn-md"
				>
					Cancelar
				</button>
				<button
					type="button"
					onclick={handleExecutarResetEstacao}
					disabled={resetandoEstacao}
					class="btn btn-error text-white btn-sm sm:btn-md gap-2"
				>
					{#if resetandoEstacao}
						<span class="loading loading-spinner loading-sm"></span>
						<span>Executando...</span>
					{:else}
						<Trash class="h-4 w-4" />
						<span>Sim, Resetar Agora</span>
					{/if}
				</button>
			</div>
		</div>
		<button
			type="button"
			class="modal-backdrop bg-black/50 cursor-default border-0"
			aria-label="Fechar"
			onclick={() => !resetandoEstacao && (modalConfirmacaoAberto = false)}
		></button>
	</div>
{/if}
