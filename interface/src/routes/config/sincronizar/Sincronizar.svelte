<script lang="ts">
	import Refresh from '~icons/tabler/refresh';
	import CloudUpload from '~icons/tabler/cloud-upload';
	import Download from '~icons/tabler/download';
	import FileSpreadsheet from '~icons/tabler/file-spreadsheet';
	import Server from '~icons/tabler/server';
	import Check from '~icons/tabler/check';
	import AlertCircle from '~icons/tabler/alert-circle';
	import Clock from '~icons/tabler/clock';
	import Stopwatch from '~icons/tabler/stopwatch';
	import ExternalLink from '~icons/tabler/external-link';
	import SettingsCard from '$lib/components/SettingsCard.svelte';
	import { notifications } from '$lib/components/toasts/notifications';
	import {
		DEFAULT_SYNC_URL,
		SYNC_URL_STORAGE_KEY,
		aplicarSyncConfigLocal,
		enviarResultadosParaEstacao,
		enviarSyncConfigParaEstacao,
		getAtletas,
		getCategorias,
		getCronometragens,
		getCronometrar,
		getProva,
		getRFIDCards,
		getRelogio,
		getSyncConfigLocal,
		getSyncResultadosLocal,
		getSyncStatusRemoto,
		mesclarResultadosLocal,
		puxarResultadosDeEstacao,
		puxarSyncConfigDeEstacao,
		sincronizarRelogioComEstacao
	} from '$lib/services/services';
	import { apurarResultadosLocais, gerarCsvClassificacaoOficial } from '$lib/utils/apuracao';
	import type {
		Atleta,
		Categoria,
		CronometragemItem,
		Cronometrar,
		Prova,
		RFIDCard,
		Relogio,
		SyncConfigPackage,
		SyncDiagnostico,
		SyncResultadosMergeResponse,
		SyncStatusResponse
	} from '$lib/types/models';
	import { onMount } from 'svelte';

	type SyncLog = {
		id: string;
		timestamp: string;
		status: 'sucesso' | 'erro';
		mensagem: string;
	};

	// P0-06: Lote de sincronização reduzido para 30 registros por requisição para evitar OOM no ESP32
	const BATCH_SIZE = 30;

	let syncUrl = $state(DEFAULT_SYNC_URL);
	let operacaoEmAndamento = $state(false);

	// Estado local da estação
	let prova = $state<Prova | undefined>();
	let categorias = $state<Categoria[]>([]);
	let atletas = $state<Atleta[]>([]);
	let rfidCards = $state<RFIDCard[]>([]);
	let relogio = $state<Relogio | undefined>();
	let cronometrar = $state<Cronometrar | undefined>();
	let cronometragens = $state<CronometragemItem[]>([]);
	let diagnosticoLocal = $state<SyncDiagnostico>({
		totalRegistros: 0,
		concluidos: 0,
		pendentesChegada: 0,
		semLargada: 0,
		invalidos: 0
	});

	// Estado remoto (quando verificado)
	let statusRemoto = $state<SyncStatusResponse | null>(null);
	let rttRemotoMs = $state<number | null>(null);

	// Opções de sincronização
	let ajustarRelogioNaSyncInicial = $state(true);
	let usarHorarioDispositivoNaImportacao = $state(false);
	let sobrescreverConflitosResultados = $state(false);

	// Resultado da última conciliação de tempos (RF-12 / TA-14)
	let ultimoResultadoMerge = $state<SyncResultadosMergeResponse | null>(null);
	let historico = $state<SyncLog[]>([]);

	let inputConfigFile: HTMLInputElement | null = null;
	let inputResultadosFile: HTMLInputElement | null = null;

	let registrosIncompletosLocais = $derived(
		cronometragens.filter(
			(item) =>
				item.status === 'sem_largada' ||
				item.status === 'invalido' ||
				(item.status === 'largada' && item.especial !== 'L0')
		)
	);

	function registrarLog(status: 'sucesso' | 'erro', mensagem: string) {
		const log: SyncLog = {
			id: Math.random().toString(36).substring(2, 9),
			timestamp: new Date().toLocaleTimeString('pt-BR'),
			status,
			mensagem
		};
		historico = [log, ...historico.slice(0, 9)];
	}

	function formatarDataHoraDispositivoISO(): string {
		const agora = new Date();
		const pad = (n: number) => String(n).padStart(2, '0');
		return `${agora.getFullYear()}-${pad(agora.getMonth() + 1)}-${pad(agora.getDate())}T${pad(agora.getHours())}:${pad(agora.getMinutes())}:${pad(agora.getSeconds())}`;
	}

	function calcularDiagnosticoLocal(lista: CronometragemItem[]): SyncDiagnostico {
		let concluidos = 0;
		let pendentesChegada = 0;
		let semLargada = 0;
		let invalidos = 0;

		for (const item of lista) {
			if (item.status === 'OK') {
				concluidos++;
			} else if (item.status === 'sem_largada') {
				semLargada++;
			} else if (item.status === 'invalido') {
				invalidos++;
			} else if (item.status === 'largada' && item.especial !== 'L0') {
				pendentesChegada++;
			}
		}

		return {
			totalRegistros: lista.length,
			concluidos,
			pendentesChegada,
			semLargada,
			invalidos
		};
	}

	async function recarregarEstadoLocal() {
		try {
			const [
				provaData,
				categoriasData,
				atletasData,
				cardsData,
				relogioData,
				cronometrarData,
				cronometragemData
			] = await Promise.all([
				getProva().catch(() => undefined),
				getCategorias().catch(() => []),
				getAtletas().catch(() => []),
				getRFIDCards().catch(() => []),
				getRelogio().catch(() => undefined),
				getCronometrar().catch(() => undefined),
				getCronometragens().catch(() => [])
			]);

			prova = provaData;
			categorias = categoriasData;
			atletas = atletasData;
			rfidCards = cardsData;
			relogio = relogioData;
			cronometrar = cronometrarData;
			cronometragens = cronometragemData;
			diagnosticoLocal = calcularDiagnosticoLocal(cronometragemData);
		} catch (e) {
			console.warn('Erro ao recarregar dados locais da estação:', e);
		}
	}

	onMount(async () => {
		try {
			const savedUrl = localStorage.getItem(SYNC_URL_STORAGE_KEY);
			if (savedUrl && savedUrl.trim() !== '') {
				syncUrl = savedUrl;
			}
		} catch (e) {
			console.warn('Erro ao ler localStorage:', e);
		}

		await recarregarEstadoLocal();
	});

	async function verificarEstacaoAlvo() {
		if (operacaoEmAndamento) return;
		operacaoEmAndamento = true;
		try {
			const { status, rttMs } = await getSyncStatusRemoto(syncUrl, 5000);
			statusRemoto = status;
			rttRemotoMs = rttMs;
			await recarregarEstadoLocal();
			const msg = `Estação alvo online (${rttMs} ms) — Relógio remoto: ${status.relogio} | Atletas: ${status.atletas ?? 0} | Registros: ${status.registros ?? 0}`;
			registrarLog('sucesso', msg);
			notifications.success('Estação alvo verificada com sucesso!', 3000);
		} catch (error: any) {
			statusRemoto = null;
			rttRemotoMs = null;
			const erroMsg = error?.message || 'Falha ao conectar com a estação alvo';
			registrarLog('erro', `Falha ao verificar ${syncUrl}: ${erroMsg}`);
			notifications.error(`Estação alvo inalcançável: ${erroMsg}`, 4000);
		} finally {
			operacaoEmAndamento = false;
		}
	}

	// =========================================================================
	// RF-06 & RF-07: Sincronização Inicial de Cadastros e Relógio
	// =========================================================================

	async function enviarConfiguracaoInicial() {
		if (operacaoEmAndamento) return;
		operacaoEmAndamento = true;
		notifications.info('Enviando cadastros e relógio para a estação alvo...', 2500);

		try {
			const resp = await enviarSyncConfigParaEstacao(syncUrl, ajustarRelogioNaSyncInicial);
			const msg = `Sincronização inicial enviada para ${syncUrl}: ${resp.categoriasSincronizadas} categorias, ${resp.atletasSincronizados} pilotos, ${resp.cartoesSincronizados} cartões RFID (Relógio: ${resp.horarioAtual}).`;
			registrarLog('sucesso', msg);
			notifications.success('Configuração inicial e relógio enviados para a estação alvo!', 3500);
		} catch (error: any) {
			const erroMsg = error?.message || 'Erro de rede na sincronização inicial';
			registrarLog('erro', `Falha ao enviar configuração para ${syncUrl}: ${erroMsg}`);
			notifications.error(`Falha na sincronização inicial: ${erroMsg}`, 4000);
		} finally {
			operacaoEmAndamento = false;
		}
	}

	async function puxarConfiguracaoInicial() {
		if (operacaoEmAndamento) return;
		operacaoEmAndamento = true;
		notifications.info('Buscando cadastros e relógio da estação de referência...', 2500);

		try {
			const resp = await puxarSyncConfigDeEstacao(syncUrl, ajustarRelogioNaSyncInicial);
			await recarregarEstadoLocal();
			const msg = `Configuração inicial recebida de ${syncUrl}: ${resp.categoriasSincronizadas} categorias, ${resp.atletasSincronizados} pilotos, ${resp.cartoesSincronizados} cartões RFID (Relógio local: ${resp.horarioAtual}).`;
			registrarLog('sucesso', msg);
			notifications.success('Esta estação foi sincronizada com a estação de referência!', 3500);
		} catch (error: any) {
			const erroMsg = error?.message || 'Erro de rede ao puxar configuração';
			registrarLog('erro', `Falha ao receber configuração de ${syncUrl}: ${erroMsg}`);
			notifications.error(`Falha ao puxar configuração inicial: ${erroMsg}`, 4000);
		} finally {
			operacaoEmAndamento = false;
		}
	}

	async function sincronizarRelogioDireto(direcao: 'enviar' | 'puxar') {
		if (operacaoEmAndamento) return;
		operacaoEmAndamento = true;
		try {
			const res = await sincronizarRelogioComEstacao(syncUrl, direcao);
			await recarregarEstadoLocal();
			const acao = direcao === 'puxar' ? 'ajustado a partir da estação alvo' : 'enviado para a estação alvo';
			const msg = `Relógio RTC DS3231 ${acao}: ${res.horarioAjustado} (RTT: ${res.rttMs} ms).`;
			registrarLog('sucesso', msg);
			notifications.success(`Relógio sincronizado (${res.horarioAjustado})!`, 3000);
		} catch (error: any) {
			const erroMsg = error?.message || 'Falha ao sincronizar relógio';
			registrarLog('erro', `Erro na sincronização de relógio: ${erroMsg}`);
			notifications.error(`Erro ao sincronizar relógio: ${erroMsg}`, 4000);
		} finally {
			operacaoEmAndamento = false;
		}
	}

	async function exportarPacoteConfigInicial() {
		if (operacaoEmAndamento) return;
		operacaoEmAndamento = true;
		try {
			const pkg = await getSyncConfigLocal();
			const dataStr =
				'data:text/json;charset=utf-8,' + encodeURIComponent(JSON.stringify(pkg, null, 2));
			const link = document.createElement('a');
			link.setAttribute('href', dataStr);
			link.setAttribute(
				'download',
				`config_inicial_estacao_${new Date().toISOString().slice(0, 10)}.json`
			);
			document.body.appendChild(link);
			link.click();
			link.remove();

			registrarLog('sucesso', 'Pacote de configuração inicial exportado (.json).');
			notifications.success('Pacote de configuração inicial exportado com sucesso!', 3000);
		} catch (error: any) {
			console.error('Erro ao exportar pacote inicial:', error);
			notifications.error('Erro ao exportar pacote de configuração inicial.', 3000);
		} finally {
			operacaoEmAndamento = false;
		}
	}

	async function aoSelecionarArquivoConfigInicial(event: Event) {
		const target = event.currentTarget as HTMLInputElement;
		const file = target.files?.[0];
		if (!file) return;

		operacaoEmAndamento = true;
		try {
			const text = await file.text();
			const parsed = JSON.parse(text) as SyncConfigPackage;

			const payload: SyncConfigPackage = {
				...parsed,
				ajustarRelogio: ajustarRelogioNaSyncInicial,
				relogio: usarHorarioDispositivoNaImportacao
					? { datetime: formatarDataHoraDispositivoISO(), offsetSeconds: 0 }
					: parsed.relogio
			};

			const resp = await aplicarSyncConfigLocal(payload);
			await recarregarEstadoLocal();

			const msg = `Pacote inicial importado (${file.name}): ${resp.categoriasSincronizadas} categorias, ${resp.atletasSincronizados} pilotos, ${resp.cartoesSincronizados} cartões RFID.`;
			registrarLog('sucesso', msg);
			notifications.success('Pacote de configuração inicial aplicado com sucesso!', 3500);
		} catch (error: any) {
			const erroMsg = error?.message || 'Arquivo JSON inválido ou incompatível';
			registrarLog('erro', `Erro ao importar configuração inicial: ${erroMsg}`);
			notifications.error(`Falha ao importar pacote: ${erroMsg}`, 4000);
		} finally {
			target.value = '';
			operacaoEmAndamento = false;
		}
	}

	// =========================================================================
	// RF-12 & RF-11: Sincronização Final, Conciliação e Exportação Real
	// =========================================================================

	async function enviarResultadosFinais() {
		if (operacaoEmAndamento) return;
		operacaoEmAndamento = true;
		notifications.info('Enviando registros de cronometragem para a estação principal...', 2500);

		try {
			const res = await enviarResultadosParaEstacao(
				syncUrl,
				sobrescreverConflitosResultados,
				(enviados, total) => {
					notifications.info(`Enviando registros (${enviados}/${total})...`, 1200);
				}
			);
			ultimoResultadoMerge = res;
			const msg = `Resultados enviados para ${syncUrl}: ${res.recebidos} enviados (${res.inseridos} novos, ${res.conciliados} conciliados, ${res.duplicadosIgnorados} já existentes, ${res.conflitosDetectados} conflitos).`;
			registrarLog('sucesso', msg);
			if (res.conflitosDetectados > 0) {
				notifications.warning(
					`Sincronizado com ${res.conflitosDetectados} conflito(s) de horário detectado(s). Verifique abaixo.`,
					4500
				);
			} else {
				notifications.success('Resultados sincronizados e confirmados pela estação principal!', 3500);
			}
		} catch (error: any) {
			const erroMsg = error?.message || 'Falha na conexão HTTP com a estação principal';
			registrarLog('erro', `Erro ao enviar resultados para ${syncUrl}: ${erroMsg}`);
			notifications.error(`Falha ao sincronizar resultados: ${erroMsg}`, 4000);
		} finally {
			operacaoEmAndamento = false;
		}
	}

	async function puxarResultadosFinais() {
		if (operacaoEmAndamento) return;
		operacaoEmAndamento = true;
		notifications.info('Recolhendo e conciliando tempos da estação alvo...', 2500);

		try {
			const res = await puxarResultadosDeEstacao(
				syncUrl,
				sobrescreverConflitosResultados,
				(enviados, total) => {
					notifications.info(`Conciliando registros (${enviados}/${total})...`, 1200);
				}
			);
			ultimoResultadoMerge = res;
			await recarregarEstadoLocal();
			const msg = `Tempos recolhidos de ${syncUrl}: ${res.recebidos} recebidos (${res.inseridos} novos, ${res.conciliados} conciliados, ${res.duplicadosIgnorados} duplicados ignorados, ${res.conflitosDetectados} conflitos).`;
			registrarLog('sucesso', msg);
			if (res.conflitosDetectados > 0) {
				notifications.warning(
					`Consolidação concluída com ${res.conflitosDetectados} conflito(s) detectado(s).`,
					4500
				);
			} else {
				notifications.success('Registros recolhidos e conciliados nesta estação!', 3500);
			}
		} catch (error: any) {
			const erroMsg = error?.message || 'Falha ao buscar registros da estação alvo';
			registrarLog('erro', `Erro ao recolher resultados de ${syncUrl}: ${erroMsg}`);
			notifications.error(`Falha ao recolher resultados: ${erroMsg}`, 4000);
		} finally {
			operacaoEmAndamento = false;
		}
	}

	async function exportarJSON() {
		if (operacaoEmAndamento) return;
		operacaoEmAndamento = true;
		try {
			const pacote = await getSyncResultadosLocal();
			const dadosParaExportar = {
				exportado_em: new Date().toISOString(),
				versao: pacote.versao ?? 1,
				prova: pacote.prova || prova,
				estacao: pacote.estacao || cronometrar,
				relogio: pacote.relogio || relogio,
				diagnostico: pacote.diagnostico || diagnosticoLocal,
				cronometragem: pacote.cronometragem || []
			};

			const dataStr =
				'data:text/json;charset=utf-8,' +
				encodeURIComponent(JSON.stringify(dadosParaExportar, null, 2));
			const downloadAnchor = document.createElement('a');
			downloadAnchor.setAttribute('href', dataStr);
			downloadAnchor.setAttribute(
				'download',
				`cronometragem_${new Date().toISOString().slice(0, 10)}.json`
			);
			document.body.appendChild(downloadAnchor);
			downloadAnchor.click();
			downloadAnchor.remove();

			registrarLog(
				'sucesso',
				`Backup JSON exportado com ${dadosParaExportar.cronometragem.length} registro(s).`
			);
			notifications.success('Arquivo JSON de cronometragem exportado com sucesso!', 3000);
		} catch (e) {
			console.error('Erro na exportação JSON:', e);
			notifications.error('Erro ao gerar exportação JSON.', 3000);
		} finally {
			operacaoEmAndamento = false;
		}
	}

	async function aoSelecionarArquivoResultados(event: Event) {
		const target = event.currentTarget as HTMLInputElement;
		const file = target.files?.[0];
		if (!file) return;

		operacaoEmAndamento = true;
		try {
			const text = await file.text();
			const parsed = JSON.parse(text);
			const lista: CronometragemItem[] = Array.isArray(parsed?.cronometragem)
				? parsed.cronometragem
				: Array.isArray(parsed?.cronometragens)
					? parsed.cronometragens
					: Array.isArray(parsed)
						? parsed
						: [];

			if (lista.length === 0) {
				throw new Error('O arquivo selecionado não contém registros de cronometragem.');
			}

			const res = await mesclarResultadosLocal(
				lista,
				sobrescreverConflitosResultados,
				(enviados, total) => {
					notifications.info(`Consolidando registros (${enviados}/${total})...`, 1200);
				}
			);
			ultimoResultadoMerge = res;
			await recarregarEstadoLocal();

			const msg = `Backup importado (${file.name}): ${res.recebidos} lidos, ${res.inseridos} novos, ${res.conciliados} conciliados, ${res.duplicadosIgnorados} duplicados ignorados, ${res.conflitosDetectados} conflitos.`;
			registrarLog('sucesso', msg);
			notifications.success('Registros do arquivo consolidados com sucesso!', 3500);
		} catch (error: any) {
			const erroMsg = error?.message || 'Arquivo de resultados inválido';
			registrarLog('erro', `Falha ao importar resultados: ${erroMsg}`);
			notifications.error(`Erro ao importar resultados: ${erroMsg}`, 4000);
		} finally {
			target.value = '';
			operacaoEmAndamento = false;
		}
	}

	async function exportarCSV() {
		if (operacaoEmAndamento) return;
		operacaoEmAndamento = true;
		try {
			await recarregarEstadoLocal();

			const mapaAtletas = new Map<number, Atleta>();
			for (const a of atletas) {
				mapaAtletas.set(a.numeral, a);
			}
			const mapaCategorias = new Map<number, string>();
			for (const c of categorias) {
				mapaCategorias.set(c.id, c.categoria);
			}

			const headers = [
				'Numeral',
				'Piloto',
				'Equipe',
				'Categoria',
				'UID_Cartao',
				'Modo',
				'Etapa_Volta',
				'Inicio',
				'Fim',
				'Duracao',
				'Duracao_Segundos',
				'Status'
			];

			const escaparCsv = (val: string | number | undefined | null): string => {
				const str = String(val ?? '').replace(/"/g, '""');
				return str.includes(';') || str.includes('"') || str.includes('\n') ? `"${str}"` : str;
			};

			const rows = cronometragens.map((item) => {
				const num = item.numeral || 0;
				const atl = num > 0 ? mapaAtletas.get(num) : undefined;
				const catNome = atl ? mapaCategorias.get(atl.categoriaId) || '' : '';
				return [
					num > 0 ? num : '',
					atl?.nome || '',
					atl?.equipe || '',
					catNome,
					item.uid || '',
					item.tipo || 'especial',
					item.especial || '',
					item.inicio || '',
					item.fim || '',
					item.duracao || '',
					typeof item.duracaoSegundos === 'number' && item.duracaoSegundos >= 0
						? item.duracaoSegundos
						: '',
					item.status || ''
				].map(escaparCsv);
			});

			const linhasTexto = [headers.join(';'), ...rows.map((r) => r.join(';'))].join('\r\n');
			const blob = new Blob(['\uFEFF' + linhasTexto], { type: 'text/csv;charset=utf-8;' });
			const url = URL.createObjectURL(blob);
			const link = document.createElement('a');
			link.setAttribute('href', url);
			link.setAttribute(
				'download',
				`relatorio_tempos_${new Date().toISOString().slice(0, 10)}.csv`
			);
			document.body.appendChild(link);
			link.click();
			link.remove();
			URL.revokeObjectURL(url);

			registrarLog('sucesso', `Planilha CSV exportada com ${cronometragens.length} registro(s).`);
			notifications.success('Relatório CSV de tempos reais exportado!', 3000);
		} catch (e) {
			console.error('Erro na exportação CSV:', e);
			notifications.error('Erro ao gerar arquivo CSV.', 3000);
		} finally {
			operacaoEmAndamento = false;
		}
	}

	async function exportarClassificacaoOficialCSV() {
		if (operacaoEmAndamento) return;
		operacaoEmAndamento = true;
		try {
			await recarregarEstadoLocal();
			const relatorio = apurarResultadosLocais({
				prova,
				categorias,
				atletas,
				rfidCards,
				estacao: cronometrar,
				cronometragens
			});

			const csv = gerarCsvClassificacaoOficial(relatorio);
			const blob = new Blob(['\uFEFF' + csv], { type: 'text/csv;charset=utf-8;' });
			const url = URL.createObjectURL(blob);
			const link = document.createElement('a');
			link.setAttribute('href', url);
			link.setAttribute(
				'download',
				`classificacao_oficial_${new Date().toISOString().slice(0, 10)}.csv`
			);
			document.body.appendChild(link);
			link.click();
			link.remove();
			URL.revokeObjectURL(url);

			registrarLog(
				'sucesso',
				`Classificação final exportada (${relatorio.classificacao.length} piloto(s) apurado(s)).`
			);
			notifications.success('Classificação oficial (CSV) exportada com sucesso!', 3000);
		} catch (e) {
			console.error('Erro ao exportar classificação oficial:', e);
			notifications.error('Erro ao gerar classificação oficial.', 3000);
		} finally {
			operacaoEmAndamento = false;
		}
	}
</script>

<SettingsCard collapsible={false} maxWidth="max-w-5xl">
	{#snippet icon()}
		<Refresh class="mr-2 inline h-6 w-6 text-primary shrink-0" />
	{/snippet}
	{#snippet title()}
		<span class="text-lg sm:text-xl font-bold">Sincronização entre Estações</span>
	{/snippet}

	<div class="space-y-4 sm:space-y-6 pt-1 w-full min-w-0">
		<!-- Cabeçalho e Status Geral -->
		<div class="bg-base-100 p-4 sm:p-6 rounded-box border border-base-200 shadow-sm w-full min-w-0">
			<div class="flex flex-col sm:flex-row sm:items-center justify-between gap-3 mb-4">
				<div class="flex items-center space-x-3">
					<div class="p-2 bg-primary/10 text-primary rounded-lg shrink-0">
						<Refresh class="h-6 w-6" />
					</div>
					<div>
						<h2 class="text-base sm:text-lg font-bold text-base-content">Sincronização entre Estações</h2>
						<p class="text-xs text-base-content/60">
							Replicação de cadastros iniciais, ajuste de relógio RTC e consolidação de resultados.
						</p>
					</div>
				</div>

				<div class="flex items-center gap-2 self-start sm:self-center">
					<button
						type="button"
						onclick={verificarEstacaoAlvo}
						disabled={operacaoEmAndamento}
						class="btn btn-outline btn-xs sm:btn-sm gap-1.5"
					>
						<Refresh class="h-3.5 w-3.5 {operacaoEmAndamento ? 'animate-spin' : ''}" />
						<span>Verificar Estação Alvo</span>
					</button>
				</div>
			</div>

			<!-- Status Grid -->
			<div class="grid grid-cols-1 md:grid-cols-3 gap-3 mb-4">
				<div class="p-3 bg-gray-50 rounded border border-gray-100 flex items-start space-x-3">
					<Server class="h-5 w-5 text-gray-500 shrink-0 mt-0.5" />
					<div class="text-xs">
						<span class="font-semibold text-gray-700 block">Estação Alvo Configurada:</span>
						<span class="text-gray-600 font-mono break-all">{syncUrl}</span>
						{#if statusRemoto}
							<span class="block text-green-700 font-medium mt-1">
								Online ({rttRemotoMs} ms) — RTC: {statusRemoto.relogio}
							</span>
						{/if}
						<a
							href="/config/url-sincronizacao"
							class="text-indigo-600 hover:underline flex items-center gap-1 mt-1 font-sans"
						>
							<span>Alterar endereço</span>
							<ExternalLink class="h-3 w-3" />
						</a>
					</div>
				</div>

				<div class="p-3 bg-gray-50 rounded border border-gray-100 flex items-start space-x-3">
					<Stopwatch class="h-5 w-5 text-gray-500 shrink-0 mt-0.5" />
					<div class="text-xs">
						<span class="font-semibold text-gray-700 block">Cadastros nesta Estação:</span>
						<div class="text-gray-600 space-y-0.5 mt-0.5">
							<div>Prova: <strong>{prova?.nome || 'Não configurada'}</strong></div>
							<div>
								Categorias: <strong>{categorias.length}</strong> | Pilotos:
								<strong>{atletas.length}</strong> | Cartões: <strong>{rfidCards.length}</strong>
							</div>
						</div>
					</div>
				</div>

				<div class="p-3 bg-gray-50 rounded border border-gray-100 flex items-start space-x-3">
					<Clock class="h-5 w-5 text-gray-500 shrink-0 mt-0.5" />
					<div class="text-xs">
						<span class="font-semibold text-gray-700 block">Relógio RTC DS3231 Local:</span>
						<span class="text-gray-800 font-mono font-semibold">{relogio?.datetime || '—'}</span>
						<div class="flex items-center gap-1 mt-1">
							<span
								class="inline-block w-2 h-2 rounded-full {cronometrar?.cronometrar
									? 'bg-green-500'
									: 'bg-gray-400'}"
							></span>
							<span class="text-gray-600">
								{cronometrar?.cronometrar ? 'Cronometrando' : 'Parada'} ({cronometrar?.tipo ||
									'especial'} - {cronometrar?.posicao || 'inicio'})
							</span>
						</div>
					</div>
				</div>
			</div>

			<!-- Resumo de Integridade dos Registros Locais -->
			<div class="grid grid-cols-2 sm:grid-cols-4 gap-2 pt-2 border-t border-gray-100 text-center">
				<div class="p-2 rounded bg-slate-50 border border-slate-200">
					<div class="text-xs text-slate-500">Total de Registros</div>
					<div class="text-base font-bold text-slate-800">{diagnosticoLocal.totalRegistros}</div>
				</div>
				<div class="p-2 rounded bg-green-50 border border-green-200">
					<div class="text-xs text-green-700">Concluídos (Início+Fim)</div>
					<div class="text-base font-bold text-green-800">{diagnosticoLocal.concluidos}</div>
				</div>
				<div class="p-2 rounded bg-blue-50 border border-blue-200">
					<div class="text-xs text-blue-700">Aguardando Chegada</div>
					<div class="text-base font-bold text-blue-800">{diagnosticoLocal.pendentesChegada}</div>
				</div>
				<div class="p-2 rounded bg-amber-50 border border-amber-200">
					<div class="text-xs text-amber-700">Chegada sem Largada</div>
					<div class="text-base font-bold text-amber-800">{diagnosticoLocal.semLargada}</div>
				</div>
			</div>
		</div>

		<!-- Etapa 1: Sincronização Inicial e de Relógios (RF-06 e RF-07) -->
		<div class="bg-base-100 p-4 sm:p-6 rounded-box border border-base-200 shadow-sm w-full min-w-0 space-y-4">
			<div>
				<h3 class="text-base sm:text-lg font-bold text-base-content">
					1. Sincronização Inicial de Cadastros e Relógio (Antes da Prova)
				</h3>
				<p class="text-xs text-base-content/60 mt-0.5">
					Replica Prova, Categorias, Pilotos, Cartões RFID e ajusta o relógio RTC DS3231 entre a estação de referência e a outra estação sem duplicar registros.
				</p>
			</div>

			<div class="flex flex-col sm:flex-row sm:items-center gap-3 text-xs text-base-content/80 bg-base-200/50 p-3 rounded-box border border-base-200">
				<label class="inline-flex items-center gap-2 cursor-pointer">
					<input
						type="checkbox"
						bind:checked={ajustarRelogioNaSyncInicial}
						class="checkbox checkbox-primary checkbox-xs"
					/>
					<span>Ajustar automaticamente o relógio RTC DS3231 durante a sincronização inicial</span>
				</label>

				<label class="inline-flex items-center gap-2 cursor-pointer">
					<input
						type="checkbox"
						bind:checked={usarHorarioDispositivoNaImportacao}
						class="checkbox checkbox-primary checkbox-xs"
					/>
					<span>Ao importar arquivo offline, ajustar RTC pelo horário deste dispositivo</span>
				</label>
			</div>

			<div class="grid grid-cols-1 md:grid-cols-2 gap-4">
				<!-- Sincronização Inicial via Wi-Fi Direto -->
				<div class="p-3 sm:p-4 rounded-box border border-base-200 bg-base-200/30 space-y-3">
					<h4 class="text-xs sm:text-sm font-bold text-base-content">Via Rede Wi-Fi (Estação ↔ Estação)</h4>
					<p class="text-xs text-base-content/60">
						Quando o navegador alcança a estação alvo ({syncUrl}) na rede:
					</p>
					<div class="flex flex-wrap gap-2">
						<button
							type="button"
							onclick={enviarConfiguracaoInicial}
							disabled={operacaoEmAndamento}
							class="btn btn-primary btn-xs sm:btn-sm gap-1.5"
						>
							<CloudUpload class="h-4 w-4" />
							<span>Enviar Cadastros + Relógio</span>
						</button>

						<button
							type="button"
							onclick={puxarConfiguracaoInicial}
							disabled={operacaoEmAndamento}
							class="btn btn-info btn-xs sm:btn-sm text-white gap-1.5"
						>
							<Download class="h-4 w-4" />
							<span>Puxar Cadastros + Relógio</span>
						</button>
					</div>

					<div class="pt-2 border-t border-base-200 flex flex-wrap items-center gap-2">
						<span class="text-xs text-base-content/60">Apenas Relógio:</span>
						<button
							type="button"
							onclick={() => sincronizarRelogioDireto('puxar')}
							disabled={operacaoEmAndamento}
							class="btn btn-outline btn-xs"
						>
							Puxar Hora da Alvo
						</button>
						<button
							type="button"
							onclick={() => sincronizarRelogioDireto('enviar')}
							disabled={operacaoEmAndamento}
							class="btn btn-outline btn-xs"
						>
							Enviar Hora para Alvo
						</button>
					</div>
				</div>

				<!-- Sincronização Inicial via Pacote no Navegador (Troca de AP Wi-Fi) -->
				<div class="p-3 sm:p-4 rounded-box border border-base-200 bg-base-200/30 space-y-3">
					<h4 class="text-xs sm:text-sm font-bold text-base-content">
						Via Arquivo Offline (Troca de Wi-Fi AP em Campo)
					</h4>
					<p class="text-xs text-base-content/60">
						Exporte o pacote na Estação de Referência, conecte no Wi-Fi da outra estação e importe:
					</p>
					<div class="flex flex-wrap gap-2">
						<button
							type="button"
							onclick={exportarPacoteConfigInicial}
							disabled={operacaoEmAndamento}
							class="btn btn-outline btn-xs sm:btn-sm gap-1.5"
						>
							<Download class="h-4 w-4" />
							<span>Exportar Pacote (.json)</span>
						</button>

						<input
							bind:this={inputConfigFile}
							type="file"
							accept=".json,application/json"
							onchange={aoSelecionarArquivoConfigInicial}
							class="hidden"
						/>
						<button
							type="button"
							onclick={() => inputConfigFile?.click()}
							disabled={operacaoEmAndamento}
							class="btn btn-success btn-xs sm:btn-sm text-white gap-1.5"
						>
							<CloudUpload class="h-4 w-4" />
							<span>Importar Pacote (.json)</span>
						</button>
					</div>
				</div>
			</div>
		</div>

		<!-- Etapa 2: Sincronização Final e Consolidação de Resultados (RF-12 e RF-11) -->
		<div class="bg-base-100 p-4 sm:p-6 rounded-box border border-base-200 shadow-sm w-full min-w-0 space-y-4">
			<div>
				<h3 class="text-base sm:text-lg font-bold text-base-content">
					2. Sincronização Final e Consolidação de Resultados (Após a Prova)
				</h3>
				<p class="text-xs text-base-content/60 mt-0.5">
					Transfere e concilia de forma idempotente os registros de largada e chegada na estação principal, detectando registros incompletos ou divergentes.
				</p>
			</div>

			<div class="flex items-center gap-2 text-xs text-base-content/80 bg-base-200/50 p-3 rounded-box border border-base-200">
				<label class="inline-flex items-center gap-2 cursor-pointer">
					<input
						type="checkbox"
						bind:checked={sobrescreverConflitosResultados}
						class="checkbox checkbox-primary checkbox-xs"
					/>
					<span>
						Em caso de conflito de horário já preenchido para o mesmo piloto/etapa, sobrescrever com o horário recebido (por padrão preserva o local e sinaliza o conflito)
					</span>
				</label>
			</div>

			<div class="grid grid-cols-1 md:grid-cols-2 gap-4">
				<!-- Sincronização Final via Rede Wi-Fi -->
				<div class="p-3 sm:p-4 rounded-box border border-base-200 bg-base-200/30 space-y-3">
					<h4 class="text-xs sm:text-sm font-bold text-base-content">
						Consolidação via Rede Wi-Fi (Estação ↔ Estação Principal)
					</h4>
					<p class="text-xs text-base-content/60">
						Envia os tempos desta estação para a principal ou recolhe os tempos da estação remota:
					</p>
					<div class="flex flex-wrap gap-2">
						<button
							type="button"
							onclick={enviarResultadosFinais}
							disabled={operacaoEmAndamento}
							class="btn btn-primary btn-xs sm:btn-sm gap-1.5"
						>
							<CloudUpload class="h-4 w-4" />
							<span>Enviar Tempos para Central</span>
						</button>

						<button
							type="button"
							onclick={puxarResultadosFinais}
							disabled={operacaoEmAndamento}
							class="btn btn-info btn-xs sm:btn-sm text-white gap-1.5"
						>
							<Download class="h-4 w-4" />
							<span>Puxar Tempos da Central</span>
						</button>
					</div>
				</div>

				<!-- Exportação e Importação Offline / Planilha -->
				<div class="p-3 sm:p-4 rounded-box border border-base-200 bg-base-200/30 space-y-3">
					<h4 class="text-xs sm:text-sm font-bold text-base-content">
						Exportação e Consolidação por Arquivo (Backup / CSV)
					</h4>
					<p class="text-xs text-base-content/60">
						Baixe os tempos reais desta estação ou mescle o arquivo JSON de outra estação:
					</p>
					<div class="flex flex-wrap gap-2">
						<button
							type="button"
							onclick={exportarJSON}
							disabled={operacaoEmAndamento}
							class="btn btn-outline btn-xs sm:btn-sm gap-1.5"
						>
							<Download class="h-4 w-4" />
							<span>Baixar Tempos (JSON)</span>
						</button>

						<input
							bind:this={inputResultadosFile}
							type="file"
							accept=".json,application/json"
							onchange={aoSelecionarArquivoResultados}
							class="hidden"
						/>
						<button
							type="button"
							onclick={() => inputResultadosFile?.click()}
							disabled={operacaoEmAndamento}
							class="btn btn-warning btn-xs sm:btn-sm text-white gap-1.5"
						>
							<CloudUpload class="h-4 w-4" />
							<span>Importar Tempos (.json)</span>
						</button>

						<button
							type="button"
							onclick={exportarCSV}
							disabled={operacaoEmAndamento}
							class="btn btn-outline btn-success btn-xs sm:btn-sm gap-1.5"
						>
							<FileSpreadsheet class="h-4 w-4" />
							<span>Passagens (CSV)</span>
						</button>

						<button
							type="button"
							onclick={exportarClassificacaoOficialCSV}
							disabled={operacaoEmAndamento}
							class="btn btn-success btn-xs sm:btn-sm text-white gap-1.5"
						>
							<FileSpreadsheet class="h-4 w-4" />
							<span>Classificação Oficial (CSV)</span>
						</button>
					</div>
				</div>
			</div>

			<!-- Resumo da última conciliação e Conflitos Detectados -->
			{#if ultimoResultadoMerge}
				<div class="p-3 sm:p-4 rounded-box border border-primary/30 bg-primary/5 space-y-2 text-xs">
					<div class="font-bold text-base-content">
						Resultado da Última Conciliação:
					</div>
					<div class="flex flex-wrap gap-x-4 gap-y-1 text-base-content/80">
						<span>Recebidos: <strong>{ultimoResultadoMerge.recebidos}</strong></span>
						<span>Novos Inseridos: <strong>{ultimoResultadoMerge.inseridos}</strong></span>
						<span>Conciliados: <strong class="text-success">{ultimoResultadoMerge.conciliados}</strong></span>
						<span>Duplicados Ignorados: <strong>{ultimoResultadoMerge.duplicadosIgnorados}</strong></span>
						<span>Conflitos: <strong class={ultimoResultadoMerge.conflitosDetectados > 0 ? 'text-error font-bold' : 'text-base-content'}>{ultimoResultadoMerge.conflitosDetectados}</strong></span>
					</div>

					{#if ultimoResultadoMerge.conflitos && ultimoResultadoMerge.conflitos.length > 0}
						<div class="pt-2">
							<div class="font-bold text-error mb-1">
								Divergências de Horário Detectadas:
							</div>
							<div class="overflow-x-auto w-full rounded-box border border-error/30">
								<table class="table table-zebra table-xs w-full bg-base-100">
									<thead>
										<tr class="bg-error/10 text-error">
											<th>Piloto / UID</th>
											<th>Etapa</th>
											<th>Campo</th>
											<th>Horário Local</th>
											<th>Horário Remoto</th>
										</tr>
									</thead>
									<tbody>
										{#each ultimoResultadoMerge.conflitos as conf, i (`${conf.uid}_${conf.especial}_${i}`)}
											<tr>
												<td class="font-mono">#{conf.numeral || '?'} ({conf.uid})</td>
												<td class="font-semibold">{conf.especial}</td>
												<td>{conf.campo}</td>
												<td class="font-mono">{conf.valorLocal}</td>
												<td class="font-mono text-error font-bold">{conf.valorRemoto}</td>
											</tr>
										{/each}
									</tbody>
								</table>
							</div>
						</div>
					{/if}
				</div>
			{/if}

			<!-- Alerta de Registros Incompletos (Sem Largada ou Aguardando Chegada) -->
			{#if registrosIncompletosLocais.length > 0}
				<div class="p-3 sm:p-4 rounded-box border border-warning/30 bg-warning/5 space-y-2 text-xs">
					<div class="font-bold text-warning-content">
						Registros Pendentes ou Incompletos nesta Estação ({registrosIncompletosLocais.length}):
					</div>
					<div class="overflow-x-auto w-full rounded-box border border-warning/30">
						<table class="table table-zebra table-xs w-full bg-base-100">
							<thead>
								<tr class="bg-warning/10 text-warning-content">
									<th>Numeral / UID</th>
									<th>Etapa / Volta</th>
									<th>Largada</th>
									<th>Chegada</th>
									<th>Situação</th>
								</tr>
							</thead>
							<tbody>
								{#each registrosIncompletosLocais as inc, idx (`${inc.uid}_${inc.especial}_${idx}`)}
									<tr>
										<td class="font-mono">#{inc.numeral || '?'} ({inc.uid})</td>
										<td class="font-semibold">{inc.especial}</td>
										<td class="font-mono">{inc.inicio || '—'}</td>
										<td class="font-mono">{inc.fim || '—'}</td>
										<td class="font-medium text-warning">
											{inc.status === 'sem_largada'
												? 'Chegada sem Largada'
												: inc.status === 'largada'
													? 'Largada sem Chegada'
													: 'Horário Inválido'}
										</td>
									</tr>
								{/each}
							</tbody>
						</table>
					</div>
				</div>
			{/if}
		</div>

		<!-- Log de Atividades da Sessão -->
		{#if historico.length > 0}
			<div class="bg-base-100 p-4 sm:p-6 rounded-box border border-base-200 shadow-sm w-full min-w-0">
				<h3 class="text-sm sm:text-base font-bold text-base-content mb-3">
					Histórico de Sincronizações Recentes
				</h3>
				<ul class="divide-y divide-base-200">
					{#each historico as log (log.id)}
						<li class="py-2 flex items-center justify-between text-xs">
							<div class="flex items-center space-x-2 min-w-0">
								{#if log.status === 'sucesso'}
									<span class="p-1 bg-success/20 text-success rounded-full shrink-0">
										<Check class="h-3.5 w-3.5" />
									</span>
								{:else}
									<span class="p-1 bg-warning/20 text-warning rounded-full shrink-0">
										<AlertCircle class="h-3.5 w-3.5" />
									</span>
								{/if}
								<span class="text-base-content/80 truncate">{log.mensagem}</span>
							</div>
							<span class="text-base-content/50 font-mono shrink-0 ml-2">{log.timestamp}</span>
						</li>
					{/each}
				</ul>
			</div>
		{/if}
	</div>
</SettingsCard>
