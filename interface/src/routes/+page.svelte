<script lang="ts">
	import { onDestroy, onMount } from 'svelte';
	import logo from '$lib/assets/logo.png';
	import Refresh from '~icons/tabler/refresh';
	import Download from '~icons/tabler/download';
	import FileSpreadsheet from '~icons/tabler/file-spreadsheet';
	import Stopwatch from '~icons/tabler/stopwatch';
	import Clock from '~icons/tabler/clock';
	import Check from '~icons/tabler/check';
	import AlertCircle from '~icons/tabler/alert-circle';
	import { getResultadosPublicos } from '$lib/services/services';
	import {
		CRITERIO_MELHOR_VOLTA,
		CRITERIO_VOLTAS_TEMPO,
		MODO_LAPS,
		gerarCsvClassificacaoOficial,
		reordenarClassificacaoPorCriterio
	} from '$lib/utils/apuracao';
	import type {
		Categoria,
		CriterioVoltas,
		PilotoApurado,
		RelatorioResultadosPublicos
	} from '$lib/types/models';

	let relatorio = $state<RelatorioResultadosPublicos | null>(null);
	let carregando = $state(true);
	let atualizando = $state(false);
	let erroCarregamento = $state('');
	let ultimaAtualizacao = $state('');

	let filtroCategoriaId = $state<number | null>(null);
	let buscaTexto = $state('');
	let criterioVoltasSelecionado = $state<CriterioVoltas>(CRITERIO_VOLTAS_TEMPO);
	let criterioInicializadoDaProva = $state(false);
	let autoAtualizar = $state(true);
	let pilotoExpandidoKey = $state<string | null>(null);

	let timerAutoRefresh: ReturnType<typeof setInterval> | null = null;

	async function carregarResultados(silencioso = false) {
		if (atualizando) return;
		if (!silencioso) {
			atualizando = true;
		}
		erroCarregamento = '';

		try {
			const dados = await getResultadosPublicos(
				criterioInicializadoDaProva ? criterioVoltasSelecionado : undefined
			);
			relatorio = dados;
			if (!criterioInicializadoDaProva && dados.criterioVoltas) {
				criterioVoltasSelecionado =
					dados.criterioVoltas === CRITERIO_MELHOR_VOLTA
						? CRITERIO_MELHOR_VOLTA
						: CRITERIO_VOLTAS_TEMPO;
				criterioInicializadoDaProva = true;
			}
			ultimaAtualizacao = new Date().toLocaleTimeString('pt-BR');
		} catch (e: any) {
			console.error('Erro ao carregar resultados públicos:', e);
			erroCarregamento = e?.message || 'Não foi possível carregar os resultados da estação.';
		} finally {
			carregando = false;
			atualizando = false;
		}
	}

	function configurarTimerAutoRefresh() {
		if (timerAutoRefresh) {
			clearInterval(timerAutoRefresh);
			timerAutoRefresh = null;
		}
		if (autoAtualizar) {
			timerAutoRefresh = setInterval(() => {
				carregarResultados(true);
			}, 10000);
		}
	}

	onMount(() => {
		carregarResultados(false);
		configurarTimerAutoRefresh();
	});

	onDestroy(() => {
		if (timerAutoRefresh) {
			clearInterval(timerAutoRefresh);
		}
	});

	let modoProva = $derived(relatorio?.modoProva || 'especial');
	let isModoVoltas = $derived(modoProva === MODO_LAPS);

	let classificacaoOrdenada = $derived.by<PilotoApurado[]>(() => {
		if (!relatorio || !Array.isArray(relatorio.classificacao)) {
			return [];
		}
		return reordenarClassificacaoPorCriterio(
			relatorio.classificacao,
			modoProva,
			criterioVoltasSelecionado
		);
	});

	let categoriasDisponiveis = $derived.by<Categoria[]>(() => {
		return relatorio?.categorias || [];
	});

	let classificacaoFiltrada = $derived.by<PilotoApurado[]>(() => {
		const termo = buscaTexto.trim().toLowerCase();
		return classificacaoOrdenada.filter((p) => {
			if (filtroCategoriaId !== null && p.categoriaId !== filtroCategoriaId) {
				return false;
			}
			if (!termo) {
				return true;
			}
			const numStr = String(p.numeral || '');
			return (
				numStr.includes(termo.replace(/^#/, '')) ||
				p.nome.toLowerCase().includes(termo) ||
				p.equipe.toLowerCase().includes(termo) ||
				p.categoriaNome.toLowerCase().includes(termo)
			);
		});
	});

	let resumoContagem = $derived.by(() => {
		let classificados = 0;
		let emCurso = 0;
		let dnf = 0;
		let dns = 0;
		for (const p of classificacaoOrdenada) {
			if (p.situacao === 'CLASSIFICADO') classificados++;
			else if (p.situacao === 'EM_CURSO') emCurso++;
			else if (p.situacao === 'DNF') dnf++;
			else dns++;
		}
		return {
			total: classificacaoOrdenada.length,
			classificados,
			emCurso,
			dnf,
			dns
		};
	});

	function chavePiloto(p: PilotoApurado): string {
		return `${p.numeral}_${p.uid || p.nome}`;
	}

	function alternarExpansao(p: PilotoApurado) {
		const key = chavePiloto(p);
		pilotoExpandidoKey = pilotoExpandidoKey === key ? null : key;
	}

	function aoAlternarAutoRefresh() {
		configurarTimerAutoRefresh();
	}

	function exportarClassificacaoCSV() {
		if (!relatorio) return;
		const relatorioAtualizado: RelatorioResultadosPublicos = {
			...relatorio,
			criterioVoltas: criterioVoltasSelecionado,
			classificacao: classificacaoOrdenada
		};
		const csv = gerarCsvClassificacaoOficial(relatorioAtualizado, filtroCategoriaId);
		const blob = new Blob(['\uFEFF' + csv], { type: 'text/csv;charset=utf-8;' });
		const url = URL.createObjectURL(blob);
		const link = document.createElement('a');
		link.setAttribute('href', url);
		const sufixoStatus = relatorio.resultadoOficial ? 'oficial' : 'parcial';
		link.setAttribute(
			'download',
			`resultado_${sufixoStatus}_${new Date().toISOString().slice(0, 10)}.csv`
		);
		document.body.appendChild(link);
		link.click();
		link.remove();
		URL.revokeObjectURL(url);
	}

	function exportarClassificacaoJSON() {
		if (!relatorio) return;
		const payload = {
			exportado_em: new Date().toISOString(),
			...relatorio,
			criterioVoltas: criterioVoltasSelecionado,
			classificacao: classificacaoOrdenada
		};
		const dataStr =
			'data:text/json;charset=utf-8,' + encodeURIComponent(JSON.stringify(payload, null, 2));
		const link = document.createElement('a');
		link.setAttribute('href', dataStr);
		const sufixoStatus = relatorio.resultadoOficial ? 'oficial' : 'parcial';
		link.setAttribute(
			'download',
			`resultado_${sufixoStatus}_${new Date().toISOString().slice(0, 10)}.json`
		);
		document.body.appendChild(link);
		link.click();
		link.remove();
	}

	function rotuloStatusEtapa(status: string): string {
		switch (status) {
			case 'OK':
				return 'Concluído';
			case 'largada':
				return 'Em Pista (Largou)';
			case 'sem_largada':
				return 'Sem Largada';
			case 'invalido':
				return 'Inválido';
			default:
				return 'Pendente';
		}
	}

	function classeBadgeStatusEtapa(status: string): string {
		switch (status) {
			case 'OK':
				return 'bg-emerald-100 text-emerald-800 border-emerald-200';
			case 'largada':
				return 'bg-blue-100 text-blue-800 border-blue-200';
			case 'sem_largada':
			case 'invalido':
				return 'bg-amber-100 text-amber-800 border-amber-200';
			default:
				return 'bg-gray-100 text-gray-600 border-gray-200';
		}
	}
</script>

<div class="min-h-screen bg-slate-100 flex flex-col">
	<!-- Cabeçalho Principal -->
	<header class="bg-white shadow-sm border-b border-slate-200 px-4 py-4">
		<div class="max-w-6xl mx-auto flex flex-col md:flex-row md:items-center md:justify-between gap-4">
			<div class="flex items-center gap-3">
				<img src={logo} alt="Logo" class="w-11 h-11 rounded object-contain bg-slate-50 p-1 border" />
				<div>
					<div class="flex flex-wrap items-center gap-2">
						<h1 class="text-xl sm:text-2xl font-bold text-slate-900">
							{relatorio?.prova?.nome || 'Cronometragem Enduro MTB'}
						</h1>
						{#if relatorio}
							{#if relatorio.statusResultado === 'oficial'}
								<span class="inline-flex items-center gap-1 px-2.5 py-0.5 rounded-full text-xs font-semibold bg-emerald-100 text-emerald-800 border border-emerald-300">
									<Check class="h-3.5 w-3.5" />
									Resultado Oficial
								</span>
							{:else if relatorio.statusResultado === 'parcial'}
								<span class="inline-flex items-center gap-1 px-2.5 py-0.5 rounded-full text-xs font-semibold bg-amber-100 text-amber-900 border border-amber-300">
									<Stopwatch class="h-3.5 w-3.5" />
									Resultado Parcial (Ao Vivo)
								</span>
							{:else}
								<span class="inline-flex items-center gap-1 px-2.5 py-0.5 rounded-full text-xs font-semibold bg-slate-100 text-slate-700 border border-slate-300">
									<Clock class="h-3.5 w-3.5" />
									Aguardando Largadas
								</span>
							{/if}
						{/if}
					</div>

					<div class="text-xs sm:text-sm text-slate-600 flex flex-wrap items-center gap-x-3 gap-y-1 mt-1">
						{#if relatorio?.prova?.local || relatorio?.prova?.data}
							<span>
								{relatorio?.prova?.local || 'Local não informado'}
								{relatorio?.prova?.data ? ` • ${relatorio.prova.data}` : ''}
							</span>
						{/if}
						<span class="font-medium text-indigo-700">
							{#if isModoVoltas}
								Modo Voltas ({relatorio?.modoVoltas === 'duas' ? 'Duas Estações' : 'Circuito Estação Única'})
							{:else}
								Modo Estágios Especiais (E1 a E{relatorio?.totalEstagiosConfigurados || 5})
							{/if}
						</span>
						{#if ultimaAtualizacao}
							<span class="text-slate-400">Atualizado às {ultimaAtualizacao}</span>
						{/if}
					</div>
				</div>
			</div>

			<!-- Controles de Atualização e Exportação -->
			<div class="flex flex-wrap items-center gap-2">
				<label class="inline-flex items-center gap-1.5 text-xs text-slate-600 bg-slate-50 px-2.5 py-1.5 rounded border border-slate-200 cursor-pointer">
					<input
						type="checkbox"
						bind:checked={autoAtualizar}
						onchange={aoAlternarAutoRefresh}
						class="rounded border-slate-300 text-indigo-600 focus:ring-indigo-500"
					/>
					<span>Auto (10s)</span>
				</label>

				<button
					type="button"
					onclick={() => carregarResultados(false)}
					disabled={atualizando}
					class="bg-indigo-600 text-white px-3 py-1.5 rounded hover:bg-indigo-700 transition text-xs font-medium flex items-center gap-1.5 disabled:opacity-50"
				>
					<Refresh class="h-4 w-4 {atualizando ? 'animate-spin' : ''}" />
					<span>Atualizar</span>
				</button>

				{#if classificacaoOrdenada.length > 0}
					<button
						type="button"
						onclick={exportarClassificacaoCSV}
						class="bg-emerald-50 text-emerald-700 border border-emerald-200 px-2.5 py-1.5 rounded hover:bg-emerald-100 transition text-xs font-medium flex items-center gap-1"
					>
						<FileSpreadsheet class="h-4 w-4" />
						<span>CSV</span>
					</button>
					<button
						type="button"
						onclick={exportarClassificacaoJSON}
						class="bg-slate-100 text-slate-700 border border-slate-300 px-2.5 py-1.5 rounded hover:bg-slate-200 transition text-xs font-medium flex items-center gap-1"
					>
						<Download class="h-4 w-4" />
						<span>JSON</span>
					</button>
				{/if}
			</div>
		</div>
	</header>

	<!-- Conteúdo Principal -->
	<main class="flex-1 max-w-6xl w-full mx-auto p-4 space-y-4">
		{#if erroCarregamento}
			<div class="bg-red-50 border border-red-200 text-red-800 px-4 py-3 rounded flex items-center justify-between text-sm">
				<div class="flex items-center gap-2">
					<AlertCircle class="h-5 w-5 text-red-600 shrink-0" />
					<span>{erroCarregamento}</span>
				</div>
				<button
					type="button"
					onclick={() => carregarResultados(false)}
					class="text-xs font-semibold underline hover:text-red-900"
				>
					Tentar novamente
				</button>
			</div>
		{/if}

		<!-- Cards de Resumo da Apuração -->
		{#if relatorio}
			<div class="grid grid-cols-2 sm:grid-cols-4 gap-3">
				<div class="bg-white p-3 rounded shadow-sm border border-slate-200">
					<div class="text-xs text-slate-500">Classificados</div>
					<div class="text-lg font-bold text-emerald-700">{resumoContagem.classificados}</div>
					<div class="text-[11px] text-slate-400">
						de {resumoContagem.total} piloto(s)
					</div>
				</div>

				<div class="bg-white p-3 rounded shadow-sm border border-slate-200">
					<div class="text-xs text-slate-500">Em Curso / Na Pista</div>
					<div class="text-lg font-bold text-blue-700">{resumoContagem.emCurso}</div>
					<div class="text-[11px] text-slate-400">
						{relatorio.estacao?.cronometrar ? 'Cronometragem ativa' : 'Estação parada'}
					</div>
				</div>

				<div class="bg-white p-3 rounded shadow-sm border border-slate-200">
					<div class="text-xs text-slate-500">Incompletos / DNF</div>
					<div class="text-lg font-bold text-amber-700">{resumoContagem.dnf}</div>
					<div class="text-[11px] text-slate-400">
						Etapas pendentes ou sem largada
					</div>
				</div>

				<div class="bg-white p-3 rounded shadow-sm border border-slate-200">
					<div class="text-xs text-slate-500">Não Largaram (DNS)</div>
					<div class="text-lg font-bold text-slate-700">{resumoContagem.dns}</div>
					<div class="text-[11px] text-slate-400">
						Inscritos sem leitura RFID
					</div>
				</div>
			</div>
		{/if}

		<!-- Barra de Filtros: Categoria, Busca e Critério de Voltas -->
		<div class="bg-white p-4 rounded shadow-sm border border-slate-200 space-y-3">
			<div class="flex flex-col md:flex-row md:items-center justify-between gap-3">
				<!-- Abas de Categorias -->
				<div class="flex flex-wrap items-center gap-1.5">
					<button
						type="button"
						onclick={() => (filtroCategoriaId = null)}
						class="px-3 py-1.5 rounded text-xs font-semibold transition {filtroCategoriaId === null
							? 'bg-indigo-600 text-white shadow-sm'
							: 'bg-slate-100 text-slate-700 hover:bg-slate-200'}"
					>
						Geral ({classificacaoOrdenada.length})
					</button>
					{#each categoriasDisponiveis as cat (cat.id)}
						{@const qtdCat = classificacaoOrdenada.filter((p) => p.categoriaId === cat.id).length}
						<button
							type="button"
							onclick={() => (filtroCategoriaId = cat.id)}
							class="px-3 py-1.5 rounded text-xs font-semibold transition {filtroCategoriaId === cat.id
								? 'bg-indigo-600 text-white shadow-sm'
								: 'bg-slate-100 text-slate-700 hover:bg-slate-200'}"
						>
							{cat.categoria} ({qtdCat})
						</button>
					{/each}
				</div>

				<!-- Busca por Numeral, Piloto ou Equipe -->
				<div class="w-full md:w-72">
					<input
						type="search"
						bind:value={buscaTexto}
						placeholder="Buscar por numeral (#), piloto ou equipe..."
						class="w-full border border-slate-300 rounded px-3 py-1.5 text-xs focus:outline-none focus:ring-2 focus:ring-indigo-500"
					/>
				</div>
			</div>

			<!-- Seletor de Critério de Classificação para o Modo Voltas (DEC-09) -->
			{#if isModoVoltas}
				<div class="pt-2 border-t border-slate-100 flex flex-col sm:flex-row sm:items-center justify-between gap-2 text-xs">
					<span class="font-semibold text-slate-700">
						Critério de Classificação (Modo Voltas):
					</span>
					<div class="flex flex-wrap gap-2">
						<button
							type="button"
							onclick={() => (criterioVoltasSelecionado = CRITERIO_VOLTAS_TEMPO)}
							class="px-3 py-1 rounded border font-medium transition {criterioVoltasSelecionado ===
							CRITERIO_VOLTAS_TEMPO
								? 'bg-indigo-50 border-indigo-600 text-indigo-800'
								: 'bg-white border-slate-300 text-slate-600 hover:bg-slate-50'}"
						>
							Mais Voltas + Menor Tempo Total
						</button>
						<button
							type="button"
							onclick={() => (criterioVoltasSelecionado = CRITERIO_MELHOR_VOLTA)}
							class="px-3 py-1 rounded border font-medium transition {criterioVoltasSelecionado ===
							CRITERIO_MELHOR_VOLTA
								? 'bg-indigo-50 border-indigo-600 text-indigo-800'
								: 'bg-white border-slate-300 text-slate-600 hover:bg-slate-50'}"
						>
							Menor Tempo de Volta Individual (Melhor Volta)
						</button>
					</div>
				</div>
			{/if}
		</div>

		<!-- Tabela de Classificação -->
		<div class="bg-white rounded shadow-sm border border-slate-200 overflow-x-auto">
			{#if carregando}
				<div class="p-8 text-center text-slate-500 text-sm">
					Carregando classificação da prova...
				</div>
			{:else if classificacaoFiltrada.length === 0}
				<div class="p-8 text-center text-slate-500 text-sm">
					Nenhum piloto encontrado para o filtro selecionado.
				</div>
			{:else}
				<table class="w-full table-auto text-sm">
					<thead class="bg-slate-100 text-slate-700 border-b border-slate-200 text-xs uppercase">
						<tr>
							<th class="p-3 text-left">
								{filtroCategoriaId !== null ? 'Pos. Cat.' : 'Pos.'}
							</th>
							<th class="p-3 text-left">#</th>
							<th class="p-3 text-left">Piloto / Equipe</th>
							<th class="p-3 text-left hidden sm:table-cell">Categoria</th>
							{#if isModoVoltas}
								<th class="p-3 text-center">Voltas</th>
								<th class="p-3 text-left">Melhor Volta</th>
								<th class="p-3 text-left">Tempo Total</th>
							{:else}
								<th class="p-3 text-center">Etapas</th>
								<th class="p-3 text-left">Tempo Total</th>
							{/if}
							<th class="p-3 text-left">Gap</th>
							<th class="p-3 text-center">Parciais</th>
						</tr>
					</thead>
					<tbody class="divide-y divide-slate-200">
						{#each classificacaoFiltrada as r (chavePiloto(r))}
							{@const expandido = pilotoExpandidoKey === chavePiloto(r)}
							{@const posExibida =
								filtroCategoriaId !== null ? r.posicaoCategoria : r.posicaoGeral}
							{@const gapExibido =
								filtroCategoriaId !== null ? r.gapCategoria : r.gapGeral}
							<tr class="hover:bg-slate-50/80 transition {r.situacao !== 'CLASSIFICADO' ? 'bg-slate-50/40' : ''}">
								<!-- Posição / Situação -->
								<td class="p-3 whitespace-nowrap">
									{#if r.situacao === 'CLASSIFICADO' && posExibida > 0}
										<div class="flex items-center gap-1.5">
											<span
												class="inline-flex items-center justify-center w-7 h-7 rounded-full font-bold text-xs {posExibida === 1
													? 'bg-amber-400 text-amber-950'
													: posExibida === 2
														? 'bg-slate-300 text-slate-900'
														: posExibida === 3
															? 'bg-amber-700 text-white'
															: 'bg-slate-100 text-slate-700'}"
											>
												{posExibida}º
											</span>
											{#if filtroCategoriaId !== null && r.posicaoGeral > 0}
												<span class="text-[11px] text-slate-400" title="Posição Geral">
													({r.posicaoGeral}º Geral)
												</span>
											{:else if filtroCategoriaId === null && r.posicaoCategoria > 0}
												<span class="text-[11px] text-slate-400" title="Posição na Categoria">
													({r.posicaoCategoria}º Cat)
												</span>
											{/if}
										</div>
									{:else if r.situacao === 'EM_CURSO'}
										<span class="inline-block px-2 py-0.5 rounded text-xs font-semibold bg-blue-100 text-blue-800 border border-blue-200">
											EM CURSO
										</span>
									{:else if r.situacao === 'DNF'}
										<span class="inline-block px-2 py-0.5 rounded text-xs font-semibold bg-amber-100 text-amber-900 border border-amber-300">
											Incompleto / DNF
										</span>
									{:else}
										<span class="inline-block px-2 py-0.5 rounded text-xs font-semibold bg-slate-200 text-slate-700">
											DNS / Sem tempo
										</span>
									{/if}
								</td>

								<!-- Numeral -->
								<td class="p-3 font-mono font-bold text-slate-800 whitespace-nowrap">
									#{r.numeral || '?'}
								</td>

								<!-- Piloto e Equipe -->
								<td class="p-3">
									<div class="font-semibold text-slate-900">{r.nome}</div>
									<div class="text-xs text-slate-500">
										{r.equipe}
										<span class="sm:hidden"> • {r.categoriaNome}</span>
									</div>
								</td>

								<!-- Categoria -->
								<td class="p-3 hidden sm:table-cell text-xs text-slate-700">
									<span class="px-2 py-0.5 rounded bg-slate-100 border border-slate-200 font-medium">
										{r.categoriaNome}
									</span>
								</td>

								{#if isModoVoltas}
									<!-- Voltas Concluídas -->
									<td class="p-3 text-center font-mono font-semibold text-slate-800">
										{r.voltasConcluidas}
									</td>
									<!-- Melhor Volta -->
									<td class="p-3 font-mono whitespace-nowrap {criterioVoltasSelecionado === CRITERIO_MELHOR_VOLTA ? 'font-bold text-indigo-900' : 'text-slate-700'}">
										{r.melhorVolta || '—'}
									</td>
									<!-- Tempo Total -->
									<td class="p-3 font-mono whitespace-nowrap {criterioVoltasSelecionado === CRITERIO_VOLTAS_TEMPO ? 'font-bold text-slate-900' : 'text-slate-700'}">
										{r.tempoTotal || '—'}
									</td>
								{:else}
									<!-- Etapas Concluídas -->
									<td class="p-3 text-center font-mono text-xs">
										<span
											class="px-2 py-0.5 rounded font-semibold {r.etapasConcluidas === r.etapasEsperadas && r.etapasEsperadas > 0
												? 'bg-emerald-50 text-emerald-800 border border-emerald-200'
												: r.etapasConcluidas > 0
													? 'bg-amber-50 text-amber-800 border border-amber-200'
													: 'bg-slate-100 text-slate-600'}"
										>
											{r.etapasConcluidas}/{r.etapasEsperadas}
										</span>
									</td>
									<!-- Tempo Total Acumulado -->
									<td class="p-3 font-mono font-bold text-slate-900 whitespace-nowrap">
										{#if r.tempoTotal}
											{r.tempoTotal}
											{#if r.situacao === 'DNF' || r.situacao === 'EM_CURSO'}
												<span class="block text-[10px] font-sans font-normal text-amber-700">
													Parcial ({r.etapasConcluidas}/{r.etapasEsperadas} etapas)
												</span>
											{/if}
										{:else}
											—
										{/if}
									</td>
								{/if}

								<!-- Gap -->
								<td class="p-3 font-mono text-xs whitespace-nowrap {r.situacao === 'CLASSIFICADO' && posExibida === 1 ? 'text-emerald-700 font-semibold' : 'text-slate-600'}">
									{gapExibido || '—'}
								</td>

								<!-- Botão Expandir Detalhes -->
								<td class="p-3 text-center whitespace-nowrap">
									<button
										type="button"
										onclick={() => alternarExpansao(r)}
										class="text-indigo-600 hover:text-indigo-800 hover:underline text-xs font-semibold px-2 py-1"
									>
										{expandido ? 'Ocultar' : 'Ver Tempos'}
									</button>
								</td>
							</tr>

							<!-- Linha Expandida: Detalhamento por Estágio ou Volta -->
							{#if expandido}
								<tr class="bg-slate-50 border-b border-slate-200">
									<td colspan={isModoVoltas ? 9 : 8} class="p-4">
										<div class="space-y-3">
											<div class="flex flex-wrap items-center justify-between gap-2 text-xs text-slate-600">
												<div class="font-semibold text-slate-800">
													Detalhamento de {isModoVoltas ? 'Voltas' : 'Estágios Especiais'} — #{r.numeral} {r.nome}
												</div>
												<div class="flex flex-wrap items-center gap-3 font-mono">
													{#if r.uid}
														<span>Cartão RFID: <strong>{r.uid}</strong></span>
													{/if}
													{#if r.horarioLargadaInicial}
														<span>Largada Inicial (L0): <strong>{r.horarioLargadaInicial}</strong></span>
													{/if}
												</div>
											</div>

											{#if r.etapas.length === 0}
												<p class="text-xs text-slate-500 italic">
													Nenhuma etapa ou volta registrada até o momento.
												</p>
											{:else}
												<div class="overflow-x-auto">
													<table class="w-full text-xs border-collapse bg-white rounded border border-slate-200">
														<thead>
															<tr class="bg-slate-100 text-slate-700 border-b text-left">
																<th class="py-2 px-3">{isModoVoltas ? 'Volta' : 'Estágio'}</th>
																<th class="py-2 px-3">Largada</th>
																<th class="py-2 px-3">Chegada</th>
																<th class="py-2 px-3">Tempo</th>
																<th class="py-2 px-3">Pos. na Etapa</th>
																<th class="py-2 px-3">Gap na Etapa</th>
																<th class="py-2 px-3">Status</th>
															</tr>
														</thead>
														<tbody class="divide-y divide-slate-100">
															{#each r.etapas as et (et.codigo)}
																<tr>
																	<td class="py-2 px-3 font-semibold text-slate-800">
																		{isModoVoltas ? `Volta ${et.indice} (${et.codigo})` : `Especial ${et.indice} (${et.codigo})`}
																	</td>
																	<td class="py-2 px-3 font-mono text-slate-600">
																		{et.inicio || '—'}
																	</td>
																	<td class="py-2 px-3 font-mono text-slate-600">
																		{et.fim || '—'}
																	</td>
																	<td class="py-2 px-3 font-mono font-bold text-slate-900">
																		{et.duracao || '—'}
																	</td>
																	<td class="py-2 px-3 font-mono text-slate-700">
																		{et.posicaoEtapa > 0 ? `${et.posicaoEtapa}º` : '—'}
																	</td>
																	<td class="py-2 px-3 font-mono text-slate-600">
																		{et.gapEtapa || '—'}
																	</td>
																	<td class="py-2 px-3">
																		<span class="inline-block px-2 py-0.5 rounded border text-[11px] font-medium {classeBadgeStatusEtapa(et.status)}">
																			{rotuloStatusEtapa(et.status)}
																		</span>
																	</td>
																</tr>
															{/each}
														</tbody>
													</table>
												</div>
											{/if}
										</div>
									</td>
								</tr>
							{/if}
						{/each}
					</tbody>
				</table>
			{/if}
		</div>
	</main>

	<!-- Rodapé -->
	<footer class="bg-white border-t border-slate-200 p-4 text-center text-sm flex flex-col sm:flex-row items-center justify-between max-w-6xl w-full mx-auto gap-2">
		<span class="text-xs text-slate-500">
			Consulta Pública Wi-Fi de Resultados • Staged Timing System
		</span>
		<a href="/config" class="text-indigo-600 hover:underline font-medium text-xs">
			Painel do Operador (Configurações)
		</a>
	</footer>
</div>
