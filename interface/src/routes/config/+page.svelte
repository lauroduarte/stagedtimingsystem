<script lang="ts">
	import { onMount } from 'svelte';
	import type { Prova, Categoria, Atleta, Relogio, Cronometrar, RFIDCard } from '$lib/types/models';
	import {
		DEFAULT_SYNC_URL,
		SYNC_URL_STORAGE_KEY,
		getAtletas,
		getCategorias,
		getCronometrar,
		getProva,
		getRelogio,
		getRFIDCards,
		getSyncConfigLocal
	} from '$lib/services/services';
	import Refresh from '~icons/tabler/refresh';
	import Flag from '~icons/tabler/flag';
	import Clock from '~icons/tabler/clock';
	import UsersGroup from '~icons/tabler/users-group';
	import Users from '~icons/tabler/users';
	import Stopwatch from '~icons/tabler/stopwatch';
	import Link from '~icons/tabler/link';
	import Eraser from '~icons/tabler/eraser';
	import Trash from '~icons/tabler/trash';
	import ArrowRight from '~icons/tabler/arrow-right';

	let provaAtual = $state<Prova>({
		nome: '',
		local: '',
		data: '',
		tipo: 'especial',
		modoVoltas: 'unica',
		criterioVoltas: 'voltas_tempo',
		totalEstagios: 5
	});
	let categorias = $state<Categoria[]>([]);
	let atletas = $state<Atleta[]>([]);
	let relogio = $state<Relogio>({ datetime: '' });
	let cronometrar = $state<Cronometrar>({
		cronometrar: false,
		tipo: 'especial',
		modoVoltas: 'unica',
		especial: 'E1',
		posicao: 'inicio',
		intervaloMinVoltaSegundos: 10
	});
	let rfidCards = $state<RFIDCard[]>([]);
	let syncUrl = $state(DEFAULT_SYNC_URL);
	let carregando = $state(true);
	let erroCarregamento = $state('');

	async function carregarDados() {
		carregando = true;
		erroCarregamento = '';
		try {
			const savedSyncUrl = localStorage.getItem(SYNC_URL_STORAGE_KEY);
			if (savedSyncUrl && savedSyncUrl.trim() !== '') {
				syncUrl = savedSyncUrl;
			}
		} catch {
			// ignore localStorage error
		}

		try {
			const pkg = await getSyncConfigLocal();

			if (pkg?.prova) {
				provaAtual = {
					nome: pkg.prova.nome || '',
					local: pkg.prova.local || '',
					data: pkg.prova.data || '',
					tipo: pkg.prova.tipo || 'especial',
					modoVoltas: pkg.prova.modoVoltas === 'duas' ? 'duas' : 'unica',
					criterioVoltas:
						pkg.prova.criterioVoltas === 'melhor_volta' ? 'melhor_volta' : 'voltas_tempo',
					totalEstagios: pkg.prova.totalEstagios || 5
				};
			}

			categorias = Array.isArray(pkg?.categorias) ? pkg.categorias : [];
			atletas = Array.isArray(pkg?.atletas) ? pkg.atletas : [];
			if (pkg?.relogio) {
				relogio = { datetime: pkg.relogio.datetime || '' };
			}
			if (pkg?.estacao) {
				cronometrar = { ...pkg.estacao };
			}
			rfidCards = Array.isArray(pkg?.rfidCards) ? pkg.rfidCards : [];
		} catch {
			// Fallback sequencial caso o endpoint consolidado falhe temporariamente
			const falhas: string[] = [];
			try {
				const provaData = await getProva();
				if (provaData) {
					provaAtual = {
						nome: provaData.nome || '',
						local: provaData.local || '',
						data: provaData.data || '',
						tipo: provaData.tipo || 'especial',
						modoVoltas: provaData.modoVoltas === 'duas' ? 'duas' : 'unica',
						criterioVoltas:
							provaData.criterioVoltas === 'melhor_volta' ? 'melhor_volta' : 'voltas_tempo',
						totalEstagios: provaData.totalEstagios || 5
					};
				}
			} catch {
				falhas.push('prova');
			}

			try {
				categorias = await getCategorias();
			} catch {
				falhas.push('categorias');
			}

			try {
				atletas = await getAtletas();
			} catch {
				falhas.push('atletas');
			}

			try {
				const relogioData = await getRelogio();
				if (relogioData) {
					relogio = { datetime: relogioData.datetime || '' };
				}
			} catch {
				falhas.push('relógio');
			}

			try {
				const cronometrarData = await getCronometrar();
				if (cronometrarData) {
					cronometrar = { ...cronometrarData };
				}
			} catch {
				falhas.push('cronometragem');
			}

			try {
				rfidCards = await getRFIDCards();
			} catch {
				falhas.push('chaveiros RFID');
			}

			if (falhas.length > 0) {
				erroCarregamento = `Alguns dados não puderam ser carregados (${falhas.join(', ')}). Clique em Atualizar para tentar novamente.`;
			}
		} finally {
			carregando = false;
		}
	}

	onMount(() => {
		carregarDados();
	});
</script>

<div class="w-full min-w-0 max-w-6xl mx-auto px-1 sm:px-4 py-2 sm:py-6 space-y-4 sm:space-y-6">
	<!-- Cabeçalho da página -->
	<div class="bg-base-200 rounded-box p-4 sm:p-6 shadow-md flex flex-col sm:flex-row sm:items-center justify-between gap-4">
		<div>
			<h1 class="text-xl sm:text-2xl font-bold text-base-content flex items-center gap-2">
				<span>🏁</span> Resumo Geral das Configurações
			</h1>
			<p class="text-xs sm:text-sm text-base-content/70 mt-1">
				Visão consolidada do estado atual dos parâmetros desta estação do Staged Timing System.
			</p>
		</div>
		<div class="flex flex-wrap items-center gap-2 self-start sm:self-center shrink-0">
			<a
				href="/config/reset"
				class="btn btn-error btn-outline btn-xs sm:btn-sm gap-1.5"
				title="Iniciar nova prova ou resetar dados"
			>
				<Trash class="h-3.5 w-3.5 sm:h-4 sm:w-4" />
				<span>Nova Prova / Reset</span>
			</a>

			<button
				type="button"
				onclick={carregarDados}
				disabled={carregando}
				class="btn btn-outline btn-xs sm:btn-sm gap-1.5"
				title="Recarregar dados da estação"
			>
				<Refresh class="h-3.5 w-3.5 sm:h-4 sm:w-4 {carregando ? 'animate-spin' : ''}" />
				<span>{carregando ? 'Atualizando...' : 'Atualizar'}</span>
			</button>
		</div>
	</div>

	{#if erroCarregamento}
		<div class="alert alert-error shadow-sm text-xs sm:text-sm">
			<span>{erroCarregamento}</span>
		</div>
	{/if}

	{#if carregando}
		<div class="flex flex-col items-center justify-center py-16 gap-3">
			<span class="loading loading-spinner loading-lg text-primary"></span>
			<p class="text-sm text-base-content/70">Carregando dados da estação...</p>
		</div>
	{:else}
		<!-- Grade de Cards de Resumo -->
		<div class="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 gap-4 sm:gap-6">
			<!-- Card 1: Prova -->
			<div class="card bg-base-100 shadow-md border border-base-200">
				<div class="card-body p-4 sm:p-5">
					<div class="flex items-center justify-between mb-2">
						<span class="card-title text-sm sm:text-base flex items-center gap-2">
							<Flag class="h-5 w-5 text-primary shrink-0" />
							<span>Prova</span>
						</span>
						<a href="/config/prova" class="btn btn-ghost btn-xs gap-1 text-primary">
							Editar <ArrowRight class="h-3 w-3" />
						</a>
					</div>
					{#if provaAtual.nome}
						<p class="text-base sm:text-lg font-bold text-base-content break-words">{provaAtual.nome}</p>
						<p class="text-xs text-base-content/70 mt-1 break-words">
							{provaAtual.local || 'Local não informado'} • {provaAtual.data || 'Data não informada'}
						</p>
						<div class="divider my-2"></div>
						<div class="badge badge-outline badge-sm">
							{provaAtual.tipo === 'laps'
								? `Modo Voltas (${provaAtual.modoVoltas === 'duas' ? 'Duas Estações' : 'Estação Única'})`
								: `Especiais (E1 a E${provaAtual.totalEstagios})`}
						</div>
					{:else}
						<div class="alert alert-warning py-2 text-xs">
							Nenhuma prova configurada nesta estação.
						</div>
					{/if}
				</div>
			</div>

			<!-- Card 2: Status da Cronometragem -->
			<div class="card bg-base-100 shadow-md border border-base-200">
				<div class="card-body p-4 sm:p-5">
					<div class="flex items-center justify-between mb-2">
						<span class="card-title text-sm sm:text-base flex items-center gap-2">
							<Stopwatch class="h-5 w-5 text-secondary shrink-0" />
							<span>Cronometragem</span>
						</span>
						<a href="/config/cronometrar" class="btn btn-ghost btn-xs gap-1 text-secondary">
							Gerenciar <ArrowRight class="h-3 w-3" />
						</a>
					</div>
					<div class="flex items-center gap-2 mt-1">
						<span
							class="badge font-semibold text-xs {cronometrar.cronometrar
								? 'badge-success animate-pulse text-white'
								: 'badge-ghost text-base-content/70'}"
						>
							{cronometrar.cronometrar ? '● Ativo (Gravando)' : '○ Parado'}
						</span>
					</div>
					<div class="text-xs sm:text-sm mt-3 space-y-1 text-base-content/80">
						{#if cronometrar.tipo === 'laps'}
							<p>Tipo: <strong>Voltas (Laps)</strong></p>
							<p>Posição: <strong>{cronometrar.posicao === 'inicio' ? 'Largada' : 'Chegada'}</strong></p>
						{:else}
							<p>Estágio: <strong>{cronometrar.especial}</strong></p>
							<p>Posição: <strong>{cronometrar.posicao === 'inicio' ? 'Largada (Início)' : 'Chegada (Fim)'}</strong></p>
						{/if}
					</div>
				</div>
			</div>

			<!-- Card 3: Relógio RTC -->
			<div class="card bg-base-100 shadow-md border border-base-200">
				<div class="card-body p-4 sm:p-5">
					<div class="flex items-center justify-between mb-2">
						<span class="card-title text-sm sm:text-base flex items-center gap-2">
							<Clock class="h-5 w-5 text-accent shrink-0" />
							<span>Relógio RTC (DS3231)</span>
						</span>
						<a href="/config/relogio" class="btn btn-ghost btn-xs gap-1 text-accent">
							Ajustar <ArrowRight class="h-3 w-3" />
						</a>
					</div>
					{#if relogio.datetime}
						<p class="text-sm sm:text-base font-mono font-bold text-base-content mt-1 break-all">{relogio.datetime}</p>
						<div class="mt-3">
							<span class="badge badge-success badge-sm text-white">RTC Operacional</span>
						</div>
					{:else}
						<p class="text-xs sm:text-sm text-error mt-1 font-medium">Relógio não sincronizado</p>
						<div class="mt-3">
							<span class="badge badge-error badge-sm text-white">Sem leitura</span>
						</div>
					{/if}
				</div>
			</div>

			<!-- Card 4: Categorias -->
			<div class="card bg-base-100 shadow-md border border-base-200">
				<div class="card-body p-4 sm:p-5">
					<div class="flex items-center justify-between mb-2">
						<span class="card-title text-sm sm:text-base flex items-center gap-2">
							<UsersGroup class="h-5 w-5 text-info shrink-0" />
							<span>Categorias ({categorias.length})</span>
						</span>
						<a href="/config/categoria" class="btn btn-ghost btn-xs gap-1 text-info">
							Ver Todas <ArrowRight class="h-3 w-3" />
						</a>
					</div>
					{#if categorias.length > 0}
						<div class="flex flex-wrap gap-1.5 mt-2 max-h-32 overflow-y-auto">
							{#each categorias as cat}
								<span class="badge badge-neutral badge-sm">{cat.categoria}</span>
							{/each}
						</div>
					{:else}
						<p class="text-xs sm:text-sm text-base-content/60 mt-1">Nenhuma categoria cadastrada.</p>
					{/if}
				</div>
			</div>

			<!-- Card 5: Pilotos e Chaveiros RFID -->
			<div class="card bg-base-100 shadow-md border border-base-200">
				<div class="card-body p-4 sm:p-5">
					<div class="flex items-center justify-between mb-2">
						<span class="card-title text-sm sm:text-base flex items-center gap-2">
							<Eraser class="h-5 w-5 text-warning shrink-0" />
							<span>Pilotos & RFID</span>
						</span>
						<a href="/config/reset" class="btn btn-ghost btn-xs gap-1 text-warning">
							Reset / RFID <ArrowRight class="h-3 w-3" />
						</a>
					</div>
					<div class="grid grid-cols-2 gap-2 sm:gap-3 mt-2">
						<div class="bg-base-200 rounded p-2.5 sm:p-3 text-center">
							<span class="text-xs text-base-content/70">Pilotos</span>
							<p class="text-lg sm:text-xl font-bold text-base-content">{atletas.length}</p>
						</div>
						<div class="bg-base-200 rounded p-2.5 sm:p-3 text-center">
							<span class="text-xs text-base-content/70">Chaveiros</span>
							<p class="text-lg sm:text-xl font-bold text-base-content">{rfidCards.length}</p>
						</div>
					</div>
					<div class="mt-2 text-xs text-base-content/70 flex justify-between items-center">
						<span>Próximo RFID: #{rfidCards.length + 1}</span>
						<a href="/config/atleta" class="link link-primary font-medium">Ver atletas</a>
					</div>
				</div>
			</div>

			<!-- Card 6: Sincronização & Rede -->
			<div class="card bg-base-100 shadow-md border border-base-200">
				<div class="card-body p-4 sm:p-5">
					<div class="flex items-center justify-between mb-2">
						<span class="card-title text-sm sm:text-base flex items-center gap-2">
							<Link class="h-5 w-5 text-primary shrink-0" />
							<span>Sincronização</span>
						</span>
						<a href="/config/url-sincronizacao" class="btn btn-ghost btn-xs gap-1 text-primary">
							Alterar <ArrowRight class="h-3 w-3" />
						</a>
					</div>
					<p class="text-xs text-base-content/70">URL da Estação Remota / Central:</p>
					<p class="text-xs font-mono bg-base-200 p-2 rounded break-all mt-1">{syncUrl}</p>
					<div class="mt-3 flex justify-end">
						<a href="/config/sincronizar" class="btn btn-xs sm:btn-sm btn-outline btn-primary gap-1">
							<Refresh class="h-3.5 w-3.5" />
							<span>Painel de Sync</span>
						</a>
					</div>
				</div>
			</div>
		</div>
	{/if}
</div>
