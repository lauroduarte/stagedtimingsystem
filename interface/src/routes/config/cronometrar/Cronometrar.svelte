<script lang="ts">
	import SettingsCard from '$lib/components/SettingsCard.svelte';
	import { notifications } from '$lib/components/toasts/notifications';
	import { onDestroy, onMount } from 'svelte';
	import Stopwatch from '~icons/tabler/stopwatch';
	import Refresh from '~icons/tabler/refresh';

	import type { Atleta, Cronometrar, CronometragemItem, Prova } from '$lib/types/models';
	import {
		getAtletas,
		getCronometragens,
		getCronometrar,
		getProva,
		getSyncConfigLocal,
		salvarCronometrar
	} from '$lib/services/services';
	import { formatarMsParaHMS, parseHMSParaMs } from '$lib/utils/apuracao';

	type ResumoVoltasPiloto = {
		uid: string;
		numeral: number;
		nome: string;
		equipe: string;
		horarioLargadaZero: string;
		voltasConcluidas: number;
		tempoTotalSegundos: number;
		tempoTotalMs: number;
		tempoTotalFormatado: string;
		melhorVoltaFormatada: string;
		voltas: {
			numero: number;
			codigo: string;
			inicio: string;
			fim: string;
			duracao: string;
			status: string;
		}[];
	};

	let cronometrar = $state<Cronometrar>({
		cronometrar: false,
		tipo: 'especial',
		modoVoltas: 'unica',
		especial: 'E1',
		posicao: 'inicio',
		intervaloMinVoltaSegundos: 10
	});

	let prova = $state<Prova>({
		nome: '',
		data: '',
		local: '',
		tipo: 'especial',
		modoVoltas: 'unica',
		totalEstagios: 5
	});

	let atletas = $state<Atleta[]>([]);
	let registros = $state<CronometragemItem[]>([]);
	let salvando = $state(false);
	let atualizandoRegistros = $state(false);
	let pollTimer: ReturnType<typeof setInterval> | null = null;

	let provaConfigurada = $derived(
		Boolean(prova.nome.trim() && prova.data.trim() && prova.local.trim())
	);

	let totalEstagiosPermitidos = $derived(
		prova.totalEstagios && prova.totalEstagios >= 1 && prova.totalEstagios <= 10
			? prova.totalEstagios
			: 10
	);

	let estagiosDisponiveis = $derived(
		Array.from({ length: totalEstagiosPermitidos }, (_, idx) => `E${idx + 1}`)
	);

	let mapaAtletasPorNumeral = $derived.by(() => {
		const map = new Map<number, Atleta>();
		for (const a of atletas) {
			map.set(a.numeral, a);
		}
		return map;
	});

	function formatarSegundos(totalSeconds: number): string {
		if (totalSeconds < 0 || !Number.isFinite(totalSeconds)) return '—';
		const h = Math.floor(totalSeconds / 3600);
		const m = Math.floor((totalSeconds % 3600) / 60);
		const s = Math.floor(totalSeconds % 60);
		return `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`;
	}

	function nomePiloto(numeral?: number): string {
		if (!numeral || numeral <= 0) return 'Piloto não identificado';
		const a = mapaAtletasPorNumeral.get(numeral);
		return a ? `#${a.numeral} - ${a.nome}` : `#${numeral}`;
	}

	function descricaoEtapa(item: CronometragemItem): string {
		const isLap = item.tipo === 'laps' || (item.especial && item.especial.startsWith('L'));
		if (!isLap) {
			return `Estágio ${item.especial}`;
		}
		const numVolta =
			typeof item.volta === 'number' && item.volta >= 0
				? item.volta
				: Number(item.especial.replace('L', '')) || 0;
		if (numVolta === 0) {
			return 'Largada Inicial (L0)';
		}
		return `Volta ${numVolta} (${item.especial})`;
	}

	let resumosVoltasPorPiloto = $derived.by((): ResumoVoltasPiloto[] => {
		const grupos = new Map<string, CronometragemItem[]>();
		for (const item of registros) {
			const isLap = item.tipo === 'laps' || (item.especial && item.especial.startsWith('L'));
			if (!isLap) continue;
			const chave = item.numeral && item.numeral > 0 ? `num_${item.numeral}` : `uid_${item.uid}`;
			const lista = grupos.get(chave) || [];
			lista.push(item);
			grupos.set(chave, lista);
		}

		const resultado: ResumoVoltasPiloto[] = [];
		for (const [, itensPiloto] of grupos.entries()) {
			const primeiro = itensPiloto[0];
			const numeral = primeiro.numeral || 0;
			const atleta = numeral > 0 ? mapaAtletasPorNumeral.get(numeral) : undefined;

			let horarioLargadaZero = '';
			let voltasConcluidas = 0;
			let tempoTotalSegundos = 0;
			let tempoTotalMs = 0;
			let melhorVoltaMs = -1;
			const voltas: ResumoVoltasPiloto['voltas'] = [];

			const ordenados = [...itensPiloto].sort((a, b) => {
				const va = a.volta ?? (Number(a.especial.replace('L', '')) || 0);
				const vb = b.volta ?? (Number(b.especial.replace('L', '')) || 0);
				return va - vb;
			});

			for (const reg of ordenados) {
				const numVolta = reg.volta ?? (Number(reg.especial.replace('L', '')) || 0);
				if (numVolta === 0) {
					horarioLargadaZero = reg.inicio || '';
					continue;
				}
				voltas.push({
					numero: numVolta,
					codigo: reg.especial || `L${numVolta}`,
					inicio: reg.inicio || '—',
					fim: reg.fim || '—',
					duracao: reg.duracao || '—',
					status: reg.status || 'OK'
				});
				const durMs =
					typeof reg.duracaoMs === 'number' && reg.duracaoMs >= 0
						? reg.duracaoMs
						: (parseHMSParaMs(reg.duracao) ??
							(typeof reg.duracaoSegundos === 'number' && reg.duracaoSegundos >= 0
								? reg.duracaoSegundos * 1000
								: -1));
				if (reg.fim && durMs >= 0) {
					voltasConcluidas += 1;
					tempoTotalMs += durMs;
					tempoTotalSegundos = Math.floor(tempoTotalMs / 1000);
					if (melhorVoltaMs < 0 || durMs < melhorVoltaMs) {
						melhorVoltaMs = durMs;
					}
				}
			}

			resultado.push({
				uid: primeiro.uid,
				numeral,
				nome: atleta ? atleta.nome : `Piloto #${numeral || '?'}`,
				equipe: atleta ? atleta.equipe : '',
				horarioLargadaZero,
				voltasConcluidas,
				tempoTotalSegundos,
				tempoTotalMs,
				tempoTotalFormatado: voltasConcluidas > 0 ? formatarMsParaHMS(tempoTotalMs) : '—',
				melhorVoltaFormatada: melhorVoltaMs >= 0 ? formatarMsParaHMS(melhorVoltaMs) : '—',
				voltas
			});
		}

		return resultado.sort((a, b) => {
			if (b.voltasConcluidas !== a.voltasConcluidas) {
				return b.voltasConcluidas - a.voltasConcluidas;
			}
			if (a.tempoTotalMs !== b.tempoTotalMs) {
				return a.tempoTotalMs - b.tempoTotalMs;
			}
			return a.numeral - b.numeral;
		});
	});

	async function carregarRegistros() {
		if (atualizandoRegistros) return;
		atualizandoRegistros = true;
		try {
			const dados = await getCronometragens();
			registros.splice(0, registros.length, ...dados);
		} catch (error) {
			console.error('Erro ao atualizar registros de cronometragem:', error);
		} finally {
			atualizandoRegistros = false;
		}
	}

	function atualizarPolling(ativo: boolean) {
		if (pollTimer) {
			clearInterval(pollTimer);
			pollTimer = null;
		}
		if (ativo) {
			pollTimer = setInterval(() => {
				carregarRegistros();
			}, 3000);
		}
	}

	onMount(async () => {
		try {
			const pkg = await getSyncConfigLocal();
			const cronometragemData = await getCronometragens().catch(() => []);
			const cronData = pkg?.estacao;
			const provaData = pkg?.prova;
			const atletasData = Array.isArray(pkg?.atletas) ? pkg.atletas : [];

			if (provaData) {
				prova = {
					nome: provaData.nome || '',
					data: provaData.data || '',
					local: provaData.local || '',
					tipo: provaData.tipo || 'especial',
					modoVoltas: provaData.modoVoltas === 'duas' ? 'duas' : 'unica',
					totalEstagios:
						provaData.totalEstagios && provaData.totalEstagios >= 1 && provaData.totalEstagios <= 10
							? provaData.totalEstagios
							: 5
				};
			}

			atletas.splice(0, atletas.length, ...atletasData);
			registros.splice(0, registros.length, ...cronometragemData);

			if (cronData) {
				const tipoInicial =
					provaData && provaData.nome && provaData.tipo ? provaData.tipo : cronData.tipo || 'especial';
				const modoVoltasInicial =
					cronData.modoVoltas === 'duas' || cronData.modoVoltas === 'unica'
						? cronData.modoVoltas
						: provaData && provaData.modoVoltas === 'duas'
							? 'duas'
							: 'unica';
				const maxEst =
					provaData && provaData.totalEstagios ? provaData.totalEstagios : 10;
				const estagioSalvo = cronData.especial || 'E1';
				const numEstagio = Number(estagioSalvo.replace('E', '')) || 1;
				const estagioValido = numEstagio >= 1 && numEstagio <= maxEst ? estagioSalvo : 'E1';
				const intervaloValido =
					cronData.intervaloMinVoltaSegundos &&
					cronData.intervaloMinVoltaSegundos >= 3 &&
					cronData.intervaloMinVoltaSegundos <= 3600
						? cronData.intervaloMinVoltaSegundos
						: 10;

				cronometrar = {
					cronometrar: Boolean(cronData.cronometrar),
					tipo: tipoInicial,
					modoVoltas: modoVoltasInicial,
					especial: estagioValido,
					posicao: cronData.posicao || 'inicio',
					intervaloMinVoltaSegundos: intervaloValido
				};
				atualizarPolling(cronometrar.cronometrar);
				notifications.success('Cronometrar carregada com sucesso!', 3000);
			}
		} catch (error) {
			console.error('Erro ao carregar a cronometrar:', error);
			notifications.error('Erro ao carregar a cronometrar. Tente novamente.', 3000);
		}
	});

	onDestroy(() => {
		if (pollTimer) {
			clearInterval(pollTimer);
			pollTimer = null;
		}
	});

	function validarParametrosVoltas(): boolean {
		const intervalo = Number(cronometrar.intervaloMinVoltaSegundos ?? 10);
		if (!Number.isFinite(intervalo) || intervalo < 3 || intervalo > 3600) {
			notifications.error(
				'O intervalo mínimo de volta deve estar entre 3 e 3600 segundos.',
				3500
			);
			return false;
		}
		if (
			cronometrar.modoVoltas === 'duas' &&
			cronometrar.posicao !== 'inicio' &&
			cronometrar.posicao !== 'fim'
		) {
			notifications.error(
				'Selecione a posição do equipamento (Largada ou Chegada) para o modo de duas estações.',
				3500
			);
			return false;
		}
		return true;
	}

	async function alternarCronometragem() {
		if (salvando) return;

		const novoEstado = !cronometrar.cronometrar;
		if (novoEstado) {
			if (!provaConfigurada) {
				notifications.error(
					'Configure os dados da Prova antes de iniciar a cronometragem.',
					4000
				);
				return;
			}
			if (atletas.length === 0) {
				notifications.error(
					'Cadastre pelo menos um atleta antes de iniciar a cronometragem.',
					4000
				);
				return;
			}
			if (cronometrar.tipo === 'especial' && !estagiosDisponiveis.includes(cronometrar.especial)) {
				notifications.error(
					`Selecione um estágio válido entre E1 e E${totalEstagiosPermitidos}.`,
					4000
				);
				return;
			}
			if (cronometrar.tipo === 'laps' && !validarParametrosVoltas()) {
				return;
			}
		}

		salvando = true;
		try {
			const modoVoltasAtual = cronometrar.modoVoltas === 'duas' ? 'duas' : 'unica';
			const posicaoNormalizada =
				cronometrar.tipo === 'laps' && modoVoltasAtual === 'unica'
					? 'inicio'
					: cronometrar.posicao || 'inicio';
			const payload: Cronometrar = {
				...cronometrar,
				modoVoltas: modoVoltasAtual,
				posicao: posicaoNormalizada,
				intervaloMinVoltaSegundos: Number(cronometrar.intervaloMinVoltaSegundos ?? 10),
				cronometrar: novoEstado
			};
			const cronometrarData = await salvarCronometrar(payload);
			if (cronometrarData) {
				cronometrar = {
					...cronometrarData,
					modoVoltas: cronometrarData.modoVoltas || modoVoltasAtual,
					intervaloMinVoltaSegundos: cronometrarData.intervaloMinVoltaSegundos ?? 10
				};
				atualizarPolling(cronometrar.cronometrar);
				await carregarRegistros();
				if (cronometrar.cronometrar) {
					notifications.success('Cronometragem iniciada com sucesso!', 3000);
				} else {
					notifications.success('Cronometragem parada com sucesso!', 3000);
				}
			} else {
				notifications.error('Erro ao atualizar a cronometrar. Verifique as permissões.', 3000);
			}
		} catch (error) {
			console.error('Erro ao salvar a cronometrar:', error);
			notifications.error(
				'Não foi possível iniciar/parar. Verifique se a Prova e os Atletas estão cadastrados.',
				3500
			);
		} finally {
			salvando = false;
		}
	}

	async function salvarConfiguracao() {
		if (salvando || cronometrar.cronometrar) return;
		if (cronometrar.tipo === 'laps' && !validarParametrosVoltas()) {
			return;
		}
		salvando = true;
		try {
			const modoVoltasAtual = cronometrar.modoVoltas === 'duas' ? 'duas' : 'unica';
			const posicaoNormalizada =
				cronometrar.tipo === 'laps' && modoVoltasAtual === 'unica'
					? 'inicio'
					: cronometrar.posicao || 'inicio';
			const payload: Cronometrar = {
				...cronometrar,
				modoVoltas: modoVoltasAtual,
				posicao: posicaoNormalizada,
				intervaloMinVoltaSegundos: Number(cronometrar.intervaloMinVoltaSegundos ?? 10)
			};
			const cronometrarData = await salvarCronometrar(payload);
			if (cronometrarData) {
				cronometrar = {
					...cronometrarData,
					modoVoltas: cronometrarData.modoVoltas || modoVoltasAtual,
					intervaloMinVoltaSegundos: cronometrarData.intervaloMinVoltaSegundos ?? 10
				};
				notifications.success('Configurações salvas com sucesso!', 3000);
			} else {
				notifications.error('Erro ao salvar as configurações. Verifique as permissões.', 3000);
			}
		} catch (error) {
			console.error('Erro ao salvar as configurações:', error);
			notifications.error('Erro ao salvar as configurações. Tente novamente.', 3000);
		} finally {
			salvando = false;
		}
	}

	// Mantém compatibilidade com chamadas a salvar
	const salvar = alternarCronometragem;
</script>

<SettingsCard collapsible={false} maxWidth="max-w-5xl">
	{#snippet icon()}
		<Stopwatch class="mr-2 inline h-6 w-6 text-secondary shrink-0" />
	{/snippet}
	{#snippet title()}
		<span class="text-lg sm:text-xl font-bold">Cronometrar Estação</span>
	{/snippet}

	<div class="space-y-4 sm:space-y-6 pt-1 w-full min-w-0">
		<div class="bg-base-100 p-4 sm:p-6 rounded-box border border-base-200 shadow-sm w-full">
			<div class="flex flex-col sm:flex-row sm:items-center justify-between pb-3 mb-4 border-b border-base-200 gap-2">
				<div>
					<h3 class="text-base font-bold text-base-content">Status da Estação</h3>
					<p class="text-xs text-base-content/60">Controle de leitura de RFID e registro de passagens</p>
				</div>
				<div>
					{#if cronometrar.cronometrar}
						<span class="inline-flex items-center gap-1.5 px-3 py-1 rounded-full text-xs font-semibold bg-green-100 text-green-800 border border-green-200">
							<span class="w-2 h-2 rounded-full bg-green-500 animate-pulse"></span>
							Ativo (Cronometrando)
						</span>
					{:else}
						<span class="inline-flex items-center gap-1.5 px-3 py-1 rounded-full text-xs font-semibold bg-base-200 text-base-content/70 border border-base-300">
							<span class="w-2 h-2 rounded-full bg-base-content/40"></span>
							Parado
						</span>
					{/if}
				</div>
			</div>

			{#if !provaConfigurada || atletas.length === 0}
				<div class="mb-4 p-3 rounded-box bg-amber-500/10 border border-amber-500/30 text-xs text-amber-800 dark:text-amber-200">
					<strong>Pré-requisitos pendentes para iniciar:</strong>
					{#if !provaConfigurada}
						<span>Cadastre a Prova em Configuração &rarr; Prova. </span>
					{/if}
					{#if !atletas.length}
						<span>Cadastre ao menos 1 piloto em Configuração &rarr; Atleta.</span>
					{/if}
				</div>
			{/if}

			<label for="tipo" class="block mb-1.5 text-xs sm:text-sm font-semibold text-base-content">Tipo</label>
			<select
				id="tipo"
				bind:value={cronometrar.tipo}
				disabled={cronometrar.cronometrar || salvando || provaConfigurada}
				class="select select-bordered select-sm sm:select-md w-full mb-4 text-sm"
			>
				<option value="especial">Especial</option>
				<option value="laps">Laps (Voltas)</option>
			</select>
			{#if provaConfigurada}
				<p class="text-xs text-base-content/60 -mt-3 mb-4">
					Modo definido pelo cadastro da prova ({prova.nome}).
				</p>
			{/if}

			{#if cronometrar.tipo === 'especial'}
				<label for="especial" class="block mb-1.5 text-xs sm:text-sm font-semibold text-base-content">
					Especial (1 a {totalEstagiosPermitidos})
				</label>
				<select
					id="especial"
					bind:value={cronometrar.especial}
					disabled={cronometrar.cronometrar || salvando}
					class="select select-bordered select-sm sm:select-md w-full mb-4 text-sm font-mono"
				>
					{#each estagiosDisponiveis as estagio}
						<option value={estagio}>{estagio}</option>
					{/each}
				</select>

				<label for="posicao" class="block mb-1.5 text-xs sm:text-sm font-semibold text-base-content">Posição do equipamento</label>
				<select
					id="posicao"
					bind:value={cronometrar.posicao}
					disabled={cronometrar.cronometrar || salvando}
					class="select select-bordered select-sm sm:select-md w-full mb-6 text-sm"
				>
					<option value="inicio">Início (Largada)</option>
					<option value="fim">Fim (Chegada)</option>
				</select>
			{:else}
				<label for="modoVoltas" class="block mb-1.5 text-xs sm:text-sm font-semibold text-base-content">
					Modo de Operação das Voltas (Laps)
				</label>
				<select
					id="modoVoltas"
					bind:value={cronometrar.modoVoltas}
					disabled={cronometrar.cronometrar || salvando}
					class="select select-bordered select-sm sm:select-md w-full mb-3 text-sm"
				>
					<option value="unica">
						Estação Única (registrar voltas e tempo de cada volta nesta estação)
					</option>
					<option value="duas">
						Duas Estações (Largada / Chegada — cada largada/chegada representa 1 volta)
					</option>
				</select>

				{#if cronometrar.modoVoltas === 'duas'}
					<label for="posicaoVoltas" class="block mb-1.5 text-xs sm:text-sm font-semibold text-base-content">
						Posição desta estação nas Voltas
					</label>
					<select
						id="posicaoVoltas"
						bind:value={cronometrar.posicao}
						disabled={cronometrar.cronometrar || salvando}
						class="select select-bordered select-sm sm:select-md w-full mb-4 text-sm"
					>
						<option value="inicio">Início (Largada da Volta)</option>
						<option value="fim">Fim (Chegada da Volta)</option>
					</select>
				{/if}

				<label for="intervaloMinVolta" class="block mb-1.5 text-xs sm:text-sm font-semibold text-base-content">
					Tempo mínimo por volta / proteção contra leitura repetida (segundos)
				</label>
				<input
					id="intervaloMinVolta"
					type="number"
					min="3"
					max="3600"
					bind:value={cronometrar.intervaloMinVoltaSegundos}
					disabled={cronometrar.cronometrar || salvando}
					class="input input-bordered input-sm sm:input-md w-full mb-3 text-sm font-mono"
				/>

				<div class="mb-6 p-3 rounded-box bg-info/10 border border-info/30 text-xs text-base-content/80">
					{#if cronometrar.modoVoltas === 'duas'}
						<strong>Operação com Duas Estações (Largada/Chegada):</strong>
						Na estação de <em>Início</em>, a leitura do cartão abre uma nova volta (L1, L2...).
						Na estação de <em>Fim</em>, a leitura fecha a volta e calcula o tempo gasto.
					{:else}
						<strong>Operação com Estação Única:</strong>
						A 1ª leitura do cartão registra a referência inicial de largada (L0).
						Cada passagem seguinte fecha a volta atual, calcula o tempo e abre a próxima volta automaticamente.
					{/if}
				</div>
			{/if}

			<div class="flex flex-wrap items-center gap-2.5 pt-2 border-t border-base-200">
				{#if cronometrar.cronometrar}
					<button
						type="button"
						onclick={alternarCronometragem}
						disabled={salvando}
						class="btn btn-error text-white btn-sm sm:btn-md gap-2"
					>
						<span class="w-2.5 h-2.5 rounded-full bg-white animate-pulse"></span>
						<span>Parar Cronometragem</span>
					</button>
				{:else}
					<button
						type="button"
						onclick={alternarCronometragem}
						disabled={salvando}
						class="btn btn-primary btn-sm sm:btn-md gap-2"
					>
						<span class="w-2.5 h-2.5 rounded-full bg-white"></span>
						<span>Iniciar Cronometragem</span>
					</button>
				{/if}

				<button
					type="button"
					onclick={salvarConfiguracao}
					disabled={salvando || cronometrar.cronometrar}
					class="btn btn-outline btn-sm sm:btn-md"
				>
					Salvar Configurações
				</button>
			</div>
		</div>

		<!-- Histórico parcial de Voltas / Passagens na Estação (100% Responsivo) -->
		<div class="bg-base-100 p-4 sm:p-6 rounded-box border border-base-200 shadow-sm w-full min-w-0">
			<div class="flex flex-col sm:flex-row sm:items-center justify-between gap-3 pb-3 mb-4 border-b border-base-200">
				<div>
					<h3 class="text-base font-bold text-base-content">
						{cronometrar.tipo === 'laps'
							? 'Histórico Parcial de Voltas nesta Estação'
							: 'Registros de Passagens nesta Estação'}
					</h3>
					<p class="text-xs text-base-content/60">
						{cronometrar.cronometrar
							? 'Atualização automática a cada 3 segundos enquanto a cronometragem está ativa.'
							: 'Consulte abaixo as marcações e tempos registrados localmente.'}
					</p>
				</div>
				<button
					type="button"
					onclick={carregarRegistros}
					disabled={atualizandoRegistros}
					class="btn btn-outline btn-xs sm:btn-sm gap-1 self-start sm:self-center shrink-0"
				>
					<Refresh class="h-3.5 w-3.5 {atualizandoRegistros ? 'animate-spin' : ''}" />
					<span>{atualizandoRegistros ? 'Atualizando...' : 'Atualizar Agora'}</span>
				</button>
			</div>

			{#if cronometrar.tipo === 'laps' && resumosVoltasPorPiloto.length > 0}
				<div class="mb-6 space-y-3">
					<h4 class="text-xs sm:text-sm font-bold text-base-content">Resumo de Voltas por Piloto</h4>
					<div class="grid grid-cols-1 md:grid-cols-2 gap-3">
						{#each resumosVoltasPorPiloto as piloto (piloto.uid)}
							<div class="border border-base-200 rounded-box p-3 sm:p-4 bg-base-200/40">
								<div class="flex items-center justify-between mb-2">
									<div class="truncate mr-2">
										<span class="font-bold text-base-content text-sm">
											#{piloto.numeral || '?'} - {piloto.nome}
										</span>
										{#if piloto.equipe}
											<span class="text-xs text-base-content/60 ml-1">({piloto.equipe})</span>
										{/if}
									</div>
									<span class="badge badge-primary badge-sm whitespace-nowrap">
										{piloto.voltasConcluidas} {piloto.voltasConcluidas === 1 ? 'volta' : 'voltas'}
									</span>
								</div>

								<div class="text-xs text-base-content/70 flex flex-wrap gap-x-3 gap-y-1 mb-3">
									{#if piloto.horarioLargadaZero}
										<span>Largada (L0): <strong class="font-mono text-base-content">{piloto.horarioLargadaZero}</strong></span>
									{/if}
									<span>Acumulado: <strong class="font-mono text-base-content">{piloto.tempoTotalFormatado}</strong></span>
									<span>Melhor: <strong class="font-mono text-success">{piloto.melhorVoltaFormatada}</strong></span>
								</div>

								{#if piloto.voltas.length > 0}
									<div class="flex flex-wrap gap-1.5">
										{#each piloto.voltas as v (v.codigo)}
											<span class="inline-flex items-center gap-1 px-2 py-0.5 rounded text-xs border border-base-300 bg-base-100 text-base-content font-mono">
												<strong>V{v.numero}:</strong>
												{#if v.duracao && v.duracao !== '—'}
													<span class="text-success font-semibold">{v.duracao}</span>
												{:else if v.status === 'largada'}
													<span class="text-info">Em curso</span>
												{:else}
													<span class="text-warning">Sem largada</span>
												{/if}
											</span>
										{/each}
									</div>
								{/if}
							</div>
						{/each}
					</div>
				</div>
			{/if}

			{#if registros.length === 0}
				<p class="text-xs sm:text-sm text-base-content/60 py-4 text-center">Nenhuma passagem registrada nesta estação até o momento.</p>
			{:else}
				<div class="overflow-x-auto w-full rounded-box border border-base-200">
					<table class="table table-zebra table-sm w-full">
						<thead>
							<tr class="bg-base-200/50">
								<th>Piloto</th>
								<th>UID Cartão</th>
								<th>Etapa / Volta</th>
								<th>Largada (Início)</th>
								<th>Chegada (Fim)</th>
								<th>Tempo Gasto</th>
								<th>Status</th>
							</tr>
						</thead>
						<tbody>
							{#each registros as item, idx (`${item.uid}_${item.especial}_${idx}`)}
								<tr>
									<td class="font-medium text-xs sm:text-sm whitespace-nowrap">
										{nomePiloto(item.numeral)}
									</td>
									<td class="font-mono text-xs text-base-content/70 whitespace-nowrap">{item.uid}</td>
									<td class="font-medium text-xs sm:text-sm whitespace-nowrap">{descricaoEtapa(item)}</td>
									<td class="font-mono text-xs whitespace-nowrap">{item.inicio || '—'}</td>
									<td class="font-mono text-xs whitespace-nowrap">{item.fim || '—'}</td>
									<td class="font-mono text-xs font-semibold whitespace-nowrap">
										{item.duracao || '—'}
									</td>
									<td class="whitespace-nowrap">
										{#if item.status === 'OK'}
											<span class="badge badge-success badge-sm text-white">
												Concluído
											</span>
										{:else if item.status === 'largada'}
											<span class="badge badge-info badge-sm text-white">
												Largada
											</span>
										{:else if item.status === 'sem_largada'}
											<span class="badge badge-warning badge-sm">
												Sem Largada
											</span>
										{:else}
											<span class="badge badge-ghost badge-sm">
												{item.status || '—'}
											</span>
										{/if}
									</td>
								</tr>
							{/each}
						</tbody>
					</table>
				</div>
			{/if}
		</div>
	</div>
</SettingsCard>
