import { page } from '$app/state';
import { notifications } from '$lib/components/toasts/notifications';
import { user } from '$lib/stores/user';
import { get } from 'svelte/store';
import type {
	Atleta,
	Categoria,
	Prova,
	Relogio,
	RFIDCard,
	Cronometrar,
	CronometragemItem,
	SyncStatusResponse,
	SyncConfigPackage,
	SyncConfigResponse,
	SyncResultadosPackage,
	SyncResultadosMergeResponse,
	CriterioVoltas,
	RelatorioResultadosPublicos
} from '$lib/types/models';

export const DEFAULT_SYNC_URL = 'http://192.168.4.1';
export const SYNC_URL_STORAGE_KEY = 'staged_timing_sync_url';

let $user: {
	username: string;
	admin: boolean;
	bearer_token: string;
} = {
	username: '',
	admin: false,
	bearer_token: ''
};

user.subscribe((u) => {
	if (u) {
		$user = u;
	}
});

export function setUser($u: { username: string; admin: boolean; bearer_token: string }) {
	if ($u) {
		$user = $u;
	}
}

function getAuthHeader(): string {
	const currentUser = get(user) || $user;
	const token = currentUser?.bearer_token || '';
	return page.data?.features?.security ? 'Bearer ' + token : 'Basic';
}

export async function getAtletas(): Promise<Atleta[]> {
	try {
		const response = await fetch('/rest/atleta', {
			method: 'GET',
			headers: {
				Authorization: getAuthHeader(),
				'Content-Type': 'application/json'
			}
		});
		if (response.status === 200) {
			const data = await response.json();
			const atletasData = data.atletas || [];
			return atletasData;
		} else {
			throw new Error(`Erro ao carregar os atletas: ${response.status} ${response.statusText}`);
		}
	} catch (error) {
		console.error('Erro ao carregar os atletas:', error);
		throw new Error('Erro ao carregar os atletas');
	}
}

export async function getCategorias(): Promise<Categoria[]> {
	try {
		const response = await fetch('/rest/categoria', {
			method: 'GET',
			headers: {
				Authorization: getAuthHeader(),
				'Content-Type': 'application/json'
			}
		});
		if (response.status === 200) {
			const data = await response.json();
			const categoriasData = data.categorias || [];
			return categoriasData;
		} else {
			throw new Error(`Erro ao carregar as categorias: ${response.status} ${response.statusText}`);
		}
	} catch (error) {
		console.error('Erro ao carregar as categorias:', error);
		throw new Error('Erro ao carregar as categorias');
	}
}

export async function getCronometrar(): Promise<Cronometrar | undefined> {
	try {
		const response = await fetch('/rest/cronometrar', {
			method: 'GET',
			headers: {
				Authorization: getAuthHeader(),
				'Content-Type': 'application/json'
			}
		});
		if (response.status === 200) {
			const data = await response.json();
			// Ensure data is not null or undefined
			if (data && typeof data === 'object') {
				return { ...data } as Cronometrar;
			}
		} else {
			throw new Error(`Erro ao carregar o cronometrar: ${response.status} ${response.statusText}`);
		}
	} catch (error) {
		console.error('Erro ao carregar o cronometrar:', error);
		throw new Error('Erro ao carregar o cronometrar');
	}
	return;
}

export async function getProva(): Promise<Prova | undefined> {
	try {
		const response = await fetch('/rest/prova', {
			method: 'GET',
			headers: {
				Authorization: getAuthHeader(),
				'Content-Type': 'application/json'
			}
		});
		if (response.status === 200) {
			const data = await response.json();
			// Ensure data is not null or undefined
			if (data && typeof data === 'object') {
				return { ...data } as Prova;
			}
		} else {
			throw new Error(`Erro ao carregar a prova: ${response.status} ${response.statusText}`);
		}
	} catch (error) {
		console.error('Erro ao carregar a prova:', error);
		throw new Error('Erro ao carregar a prova');
	}
	return;
}

export async function getRelogio(): Promise<Relogio | undefined> {
	try {
		const response = await fetch('/rest/relogio', {
			method: 'GET',
			headers: {
				Authorization: getAuthHeader(),
				'Content-Type': 'application/json'
			}
		});
		if (response.status === 200) {
			const data = await response.json();
			// Ensure data is not null or undefined
			if (data && typeof data === 'object') {
				return { ...data } as Relogio;
			}
		} else {
			throw new Error(`Erro ao carregar o relogio: ${response.status} ${response.statusText}`);
		}
	} catch (error) {
		console.error('Erro ao carregar o relogio:', error);
		throw new Error('Erro ao carregar o relogio');
	}
	return;
}

export async function salvarCategorias(categorias: Categoria[]): Promise<Categoria[]> {
	try {
		const response = await fetch('/rest/categoria', {
			method: 'POST',
			headers: {
				Authorization: getAuthHeader(),
				'Content-Type': 'application/json'
			},
			body: JSON.stringify({ categorias })
		});
		if (response.status === 200) {
			const data = await response.json();
			const categoriasData = data.categorias || [];
			return categoriasData;
		} else {
			throw new Error(`Erro ao salvar as categorias: ${response.status} ${response.statusText}`);
		}
	} catch (error) {
		console.error('Erro ao salvar as categorias:', error);
		throw new Error('Erro ao salvar as categorias');
	}
}

export async function salvarAtletas(atletas: Atleta[]): Promise<Atleta[]> {
	try {
		const response = await fetch('/rest/atleta', {
			method: 'POST',
			headers: {
				Authorization: getAuthHeader(),
				'Content-Type': 'application/json'
			},
			body: JSON.stringify({ atletas })
		});
		if (response.status === 200) {
			const data = await response.json();
			const atletasData = data.atletas || [];
			return atletasData;
		} else {
			throw new Error(`Erro ao salvar os atletas: ${response.status} ${response.statusText}`);
		}
	} catch (error) {
		console.error('Erro ao salvar os atletas:', error);
		throw new Error('Erro ao salvar os atletas');
	}
}

export async function salvarCronometrar(cronometrar: Cronometrar): Promise<Cronometrar> {
	try {
		const response = await fetch('/rest/cronometrar', {
			method: 'POST',
			headers: {
				Authorization: getAuthHeader(),
				'Content-Type': 'application/json'
			},
			body: JSON.stringify(cronometrar)
		});
		if (response.status === 200) {
			const data = await response.json();
			cronometrar = { ...data };
			return cronometrar;
		} else {
			throw new Error(`Erro ao cronometrar: ${response.status} ${response.statusText}`);
		}
	} catch (error) {
		console.error('Erro ao cronometrar:', error);
		throw new Error('Erro ao cronometrar');
	}
}

export async function salvarProva(prova: Prova): Promise<Prova> {
	try {
		const response = await fetch('/rest/prova', {
			method: 'POST',
			headers: {
				Authorization: getAuthHeader(),
				'Content-Type': 'application/json'
			},
			body: JSON.stringify(prova)
		});
		if (response.status === 200) {
			const data = await response.json();
			prova = { ...data };
			return prova;
		} else {
			throw new Error(`Erro ao salvar a prova: ${response.status} ${response.statusText}`);
		}
	} catch (error) {
		console.error('Erro ao salvar a prova:', error);
		throw new Error('Erro ao salvar a prova');
	}
}


export async function salvarRelogio(relogio: Relogio): Promise<Relogio> {
	try {
		const response = await fetch('/rest/relogio', {
			method: 'POST',
			headers: {
				Authorization: getAuthHeader(),
				'Content-Type': 'application/json'
			},
			body: JSON.stringify(relogio)
		});
		if (response.status === 200) {
			const data = await response.json();
			relogio = { ...data };
			return relogio;
		} else {
			throw new Error(`Erro ao salvar o relogio: ${response.status} ${response.statusText}`);
		}
	} catch (error) {
		console.error('Erro ao salvar o relogio:', error);
		throw new Error('Erro ao salvar o relogio');
	}
}

export async function startRFIDReset(): Promise<{ status?: string; nextIndex?: number }> {
	console.log('Iniciando o reset do RFID...');
	try {
		const response = await fetch('/rest/rfid/start_reset', {
			method: 'GET',
			headers: {
				Authorization: getAuthHeader(),
				'Content-Type': 'application/json'
			}
		});
		if (response.status !== 200) {
			throw new Error(`Erro ao iniciar o reset do RFID: ${response.status} ${response.statusText}`);
		}
		return await response.json().catch(() => ({}));
	} catch (error) {
		console.error('Erro ao iniciar o reset do RFID:', error);
		throw new Error('Erro ao iniciar o reset do RFID');
	}
}

export async function stopRFIDReset(): Promise<RFIDCard[]> {
	try {
		const response = await fetch('/rest/rfid/stop_reset', {
			method: 'GET',
			headers: {
				Authorization: getAuthHeader(),
				'Content-Type': 'application/json'
			}
		});
		if (response.status === 200) {
			const data = await response.json();
			const cards: RFIDCard[] = Array.isArray(data?.cards)
				? data.cards
				: Array.isArray(data)
					? data
					: [];
			return cards;
		} else {
			throw new Error(`Erro ao parar o reset do RFID: ${response.status} ${response.statusText}`);
		}
	} catch (error) {
		console.error('Erro ao parar o reset do RFID:', error);
		throw new Error('Erro ao parar o reset do RFID');
	}
}

export async function getRFIDCards(): Promise<RFIDCard[]> {
	try {
		const response = await fetch('/rest/rfid', {
			method: 'GET',
			headers: {
				Authorization: getAuthHeader(),
				'Content-Type': 'application/json'
			}
		});
		if (response.status === 200) {
			const data = await response.json();
			const rfidData = data.cards || [];
			return rfidData;
		} else {
			throw new Error(`Erro ao carregar os RFIDs: ${response.status} ${response.statusText}`);
		}
	} catch (error) {
		console.error('Erro ao carregar os RFIDs:', error);
		throw new Error('Erro ao carregar os RFIDs');
	}
}

export async function getCronometragens(): Promise<CronometragemItem[]> {
	try {
		const response = await fetch('/rest/cronometragem', {
			method: 'GET',
			headers: {
				Authorization: getAuthHeader(),
				'Content-Type': 'application/json'
			}
		});
		if (response.status === 200) {
			const data = await response.json();
			return Array.isArray(data?.cronometragem) ? data.cronometragem : [];
		} else {
			throw new Error(`Erro ao carregar cronometragem: ${response.status} ${response.statusText}`);
		}
	} catch (error) {
		console.error('Erro ao carregar cronometragem:', error);
		throw new Error('Erro ao carregar cronometragem');
	}
}

export function resolverEndpointsSync(rawUrl: string): {
	origin: string;
	statusUrl: string;
	configUrl: string;
	resultadosUrl: string;
} {
	const trimmed = (rawUrl || DEFAULT_SYNC_URL).trim();
	const parsed = new URL(trimmed);
	const origin = parsed.origin;
	const pathname = parsed.pathname.replace(/\/+$/, '');

	const isStandardEspPath =
		pathname === '' ||
		pathname === '/sync' ||
		pathname.startsWith('/rest/sync');

	return {
		origin,
		statusUrl: isStandardEspPath ? `${origin}/rest/sync/status` : trimmed,
		configUrl: `${origin}/rest/sync/config`,
		resultadosUrl: isStandardEspPath ? `${origin}/rest/sync/resultados` : trimmed
	};
}

async function fetchJsonComTimeout<T>(
	url: string,
	options: RequestInit = {},
	timeoutMs = 6000
): Promise<{ data: T; rttMs: number }> {
	const controller = new AbortController();
	const timeoutId = setTimeout(() => controller.abort(), timeoutMs);
	const t0 = performance.now();

	try {
		const response = await fetch(url, {
			...options,
			signal: controller.signal
		});
		const rttMs = Math.max(0, Math.round(performance.now() - t0));
		clearTimeout(timeoutId);

		let body: any = null;
		try {
			body = await response.json();
		} catch {
			body = null;
		}

		if (!response.ok) {
			const msg = body?.mensagem || `${response.status} ${response.statusText}`;
			throw new Error(msg);
		}

		return { data: (body ?? {}) as T, rttMs };
	} catch (error: any) {
		clearTimeout(timeoutId);
		if (error?.name === 'AbortError') {
			throw new Error(`Tempo limite esgotado (${Math.round(timeoutMs / 1000)}s) ao comunicar com ${url}`);
		}
		throw error;
	}
}

export async function getSyncStatusLocal(): Promise<SyncStatusResponse> {
	const { data } = await fetchJsonComTimeout<SyncStatusResponse>('/rest/sync/status', {
		method: 'GET'
	});
	return data;
}

export async function getSyncStatusRemoto(
	rawUrl: string,
	timeoutMs = 5000
): Promise<{ status: SyncStatusResponse; rttMs: number }> {
	const endpoints = resolverEndpointsSync(rawUrl);
	const { data, rttMs } = await fetchJsonComTimeout<SyncStatusResponse>(
		endpoints.statusUrl,
		{ method: 'GET' },
		timeoutMs
	);
	return { status: data, rttMs };
}

export async function getSyncConfigLocal(): Promise<SyncConfigPackage> {
	const { data } = await fetchJsonComTimeout<SyncConfigPackage>('/rest/sync/config', {
		method: 'GET'
	});
	return data;
}

export async function aplicarSyncConfigLocal(pkg: SyncConfigPackage): Promise<SyncConfigResponse> {
	const { data } = await fetchJsonComTimeout<SyncConfigResponse>(
		'/rest/sync/config',
		{
			method: 'POST',
			headers: { 'Content-Type': 'application/json' },
			body: JSON.stringify(pkg)
		},
		8000
	);
	return data;
}

export async function enviarSyncConfigParaEstacao(
	rawUrl: string,
	ajustarRelogio = true
): Promise<SyncConfigResponse> {
	const endpoints = resolverEndpointsSync(rawUrl);
	const localPkg = await getSyncConfigLocal();
	const relogioAtual = await getRelogio().catch(() => undefined);

	const payload: SyncConfigPackage = {
		...localPkg,
		ajustarRelogio,
		relogio: {
			datetime: relogioAtual?.datetime || localPkg.relogio?.datetime || '',
			offsetSeconds: 0
		}
	};

	const { data } = await fetchJsonComTimeout<SyncConfigResponse>(
		endpoints.configUrl,
		{
			method: 'POST',
			headers: { 'Content-Type': 'application/json' },
			body: JSON.stringify(payload)
		},
		8000
	);
	return data;
}

export async function puxarSyncConfigDeEstacao(
	rawUrl: string,
	ajustarRelogio = true
): Promise<SyncConfigResponse> {
	const endpoints = resolverEndpointsSync(rawUrl);
	const { data: remotePkg, rttMs } = await fetchJsonComTimeout<SyncConfigPackage>(
		endpoints.configUrl,
		{ method: 'GET' },
		6000
	);

	const offsetCompensado = Math.round(rttMs / 2000);
	const payloadParaAplicar: SyncConfigPackage = {
		...remotePkg,
		ajustarRelogio,
		relogio: remotePkg.relogio
			? {
					...remotePkg.relogio,
					offsetSeconds: offsetCompensado
				}
			: undefined
	};

	return await aplicarSyncConfigLocal(payloadParaAplicar);
}

export async function sincronizarRelogioComEstacao(
	rawUrl: string,
	direcao: 'enviar' | 'puxar'
): Promise<{ horarioAjustado: string; rttMs: number }> {
	const endpoints = resolverEndpointsSync(rawUrl);

	if (direcao === 'puxar') {
		const { status, rttMs } = await getSyncStatusRemoto(rawUrl, 5000);
		if (!status.relogio) {
			throw new Error('A estação remota não retornou um horário válido.');
		}
		const offsetSeconds = Math.round(rttMs / 2000);
		const localResp = await aplicarSyncConfigLocal({
			ajustarRelogio: true,
			relogio: {
				datetime: status.relogio,
				offsetSeconds
			}
		});
		return {
			horarioAjustado: localResp.horarioAtual || status.relogio,
			rttMs
		};
	}

	const relogioLocal = await getRelogio();
	if (!relogioLocal?.datetime) {
		throw new Error('Não foi possível obter o horário do RTC desta estação.');
	}

	const { data, rttMs } = await fetchJsonComTimeout<SyncConfigResponse>(
		endpoints.configUrl,
		{
			method: 'POST',
			headers: { 'Content-Type': 'application/json' },
			body: JSON.stringify({
				ajustarRelogio: true,
				relogio: {
					datetime: relogioLocal.datetime,
					offsetSeconds: 0
				}
			})
		},
		6000
	);

	return {
		horarioAjustado: data.horarioAtual || relogioLocal.datetime,
		rttMs
	};
}

export async function getSyncResultadosLocal(): Promise<SyncResultadosPackage> {
	const { data } = await fetchJsonComTimeout<SyncResultadosPackage>('/rest/sync/resultados', {
		method: 'GET'
	});
	return data;
}

export const SYNC_BATCH_SIZE = 30;

export async function mesclarResultadosLocal(
	cronometragem: CronometragemItem[],
	sobrescreverConflitos = false,
	onProgress?: (enviados: number, total: number) => void
): Promise<SyncResultadosMergeResponse> {
	if (!Array.isArray(cronometragem) || cronometragem.length === 0) {
		const { data } = await fetchJsonComTimeout<SyncResultadosMergeResponse>(
			'/rest/sync/resultados',
			{
				method: 'POST',
				headers: { 'Content-Type': 'application/json' },
				body: JSON.stringify({
					cronometragem: [],
					sobrescreverConflitos
				})
			},
			8000
		);
		return data;
	}

	const resultadoConsolidado: SyncResultadosMergeResponse = {
		status: 'ok',
		recebidos: 0,
		inseridos: 0,
		conciliados: 0,
		duplicadosIgnorados: 0,
		conflitosDetectados: 0,
		totalConsolidado: 0,
		diagnostico: {
			totalRegistros: 0,
			concluidos: 0,
			pendentesChegada: 0,
			semLargada: 0,
			invalidos: 0
		},
		conflitos: []
	};

	for (let i = 0; i < cronometragem.length; i += SYNC_BATCH_SIZE) {
		const lote = cronometragem.slice(i, i + SYNC_BATCH_SIZE);
		const { data } = await fetchJsonComTimeout<SyncResultadosMergeResponse>(
			'/rest/sync/resultados',
			{
				method: 'POST',
				headers: { 'Content-Type': 'application/json' },
				body: JSON.stringify({
					cronometragem: lote,
					sobrescreverConflitos
				})
			},
			8000
		);

		resultadoConsolidado.recebidos += data.recebidos || 0;
		resultadoConsolidado.inseridos += data.inseridos || 0;
		resultadoConsolidado.conciliados += data.conciliados || 0;
		resultadoConsolidado.duplicadosIgnorados += data.duplicadosIgnorados || 0;
		resultadoConsolidado.conflitosDetectados += data.conflitosDetectados || 0;
		resultadoConsolidado.totalConsolidado = data.totalConsolidado;
		if (data.diagnostico) {
			resultadoConsolidado.diagnostico = data.diagnostico;
		}
		if (Array.isArray(data.conflitos)) {
			resultadoConsolidado.conflitos.push(...data.conflitos);
		}

		if (onProgress) {
			onProgress(Math.min(i + SYNC_BATCH_SIZE, cronometragem.length), cronometragem.length);
		}
	}

	return resultadoConsolidado;
}

export async function enviarResultadosParaEstacao(
	rawUrl: string,
	sobrescreverConflitos = false,
	onProgress?: (enviados: number, total: number) => void
): Promise<SyncResultadosMergeResponse> {
	const endpoints = resolverEndpointsSync(rawUrl);
	const pacoteLocal = await getSyncResultadosLocal();
	const registros = pacoteLocal.cronometragem || [];

	if (registros.length === 0) {
		const { data } = await fetchJsonComTimeout<SyncResultadosMergeResponse>(
			endpoints.resultadosUrl,
			{
				method: 'POST',
				headers: { 'Content-Type': 'application/json' },
				body: JSON.stringify({
					estacao: pacoteLocal.estacao,
					relogio: pacoteLocal.relogio,
					cronometragem: [],
					sobrescreverConflitos
				})
			},
			8000
		);
		return data;
	}

	const resultadoConsolidado: SyncResultadosMergeResponse = {
		status: 'ok',
		recebidos: 0,
		inseridos: 0,
		conciliados: 0,
		duplicadosIgnorados: 0,
		conflitosDetectados: 0,
		totalConsolidado: 0,
		diagnostico: {
			totalRegistros: 0,
			concluidos: 0,
			pendentesChegada: 0,
			semLargada: 0,
			invalidos: 0
		},
		conflitos: []
	};

	for (let i = 0; i < registros.length; i += SYNC_BATCH_SIZE) {
		const lote = registros.slice(i, i + SYNC_BATCH_SIZE);
		const { data } = await fetchJsonComTimeout<SyncResultadosMergeResponse>(
			endpoints.resultadosUrl,
			{
				method: 'POST',
				headers: { 'Content-Type': 'application/json' },
				body: JSON.stringify({
					estacao: pacoteLocal.estacao,
					relogio: pacoteLocal.relogio,
					cronometragem: lote,
					sobrescreverConflitos
				})
			},
			8000
		);

		resultadoConsolidado.recebidos += data.recebidos || 0;
		resultadoConsolidado.inseridos += data.inseridos || 0;
		resultadoConsolidado.conciliados += data.conciliados || 0;
		resultadoConsolidado.duplicadosIgnorados += data.duplicadosIgnorados || 0;
		resultadoConsolidado.conflitosDetectados += data.conflitosDetectados || 0;
		resultadoConsolidado.totalConsolidado = data.totalConsolidado;
		if (data.diagnostico) {
			resultadoConsolidado.diagnostico = data.diagnostico;
		}
		if (Array.isArray(data.conflitos)) {
			resultadoConsolidado.conflitos.push(...data.conflitos);
		}

		if (onProgress) {
			onProgress(Math.min(i + SYNC_BATCH_SIZE, registros.length), registros.length);
		}
	}

	return resultadoConsolidado;
}

export async function puxarResultadosDeEstacao(
	rawUrl: string,
	sobrescreverConflitos = false,
	onProgress?: (enviados: number, total: number) => void
): Promise<SyncResultadosMergeResponse> {
	const endpoints = resolverEndpointsSync(rawUrl);
	const { data: pacoteRemoto } = await fetchJsonComTimeout<SyncResultadosPackage>(
		endpoints.resultadosUrl,
		{ method: 'GET' },
		6000
	);

	const listaRemota = Array.isArray(pacoteRemoto?.cronometragem) ? pacoteRemoto.cronometragem : [];
	return await mesclarResultadosLocal(listaRemota, sobrescreverConflitos, onProgress);
}

export async function getResultadosPublicos(
	criterioVoltas?: CriterioVoltas
): Promise<RelatorioResultadosPublicos> {
	const query = criterioVoltas ? `?criterioVoltas=${encodeURIComponent(criterioVoltas)}` : '';
	const { data } = await fetchJsonComTimeout<RelatorioResultadosPublicos>(
		`/rest/resultados-publicos${query}`,
		{
			method: 'GET'
		},
		6000
	);
	return data;
}

export type ResetEstacaoOptions = {
	limparCronometragem?: boolean;
	limparProva?: boolean;
	limparAtletas?: boolean;
	limparCategorias?: boolean;
	limparRfidCards?: boolean;
	pararCronometragem?: boolean;
};

export type ResetEstacaoResponse = {
	status: string;
	mensagem: string;
	detalhes?: {
		cronometragemLimpa?: boolean;
		provaLimpa?: boolean;
		atletasLimpos?: boolean;
		categoriasLimpas?: boolean;
		rfidCardsLimpos?: boolean;
	};
};

export async function resetarDadosEstacao(
	options: ResetEstacaoOptions = {
		limparCronometragem: true,
		limparProva: true,
		limparAtletas: true,
		limparCategorias: true,
		limparRfidCards: true,
		pararCronometragem: true
	}
): Promise<ResetEstacaoResponse> {
	try {
		const response = await fetch('/rest/estacao/reset', {
			method: 'POST',
			headers: {
				Authorization: getAuthHeader(),
				'Content-Type': 'application/json'
			},
			body: JSON.stringify(options)
		});

		if (response.ok) {
			const data = await response.json();
			return data;
		}

		// Fallback: caso o endpoint dedicado retorne 404 por diferença de versão de firmware,
		// executa limpeza multi-serviço através dos endpoints REST individuais
		if (response.status === 404) {
			if (options.pararCronometragem) {
				await salvarCronometrar({
					cronometrar: false,
					tipo: 'especial',
					modoVoltas: 'unica',
					especial: 'E1',
					posicao: 'inicio',
					intervaloMinVoltaSegundos: 10
				}).catch(() => undefined);
			}

			if (options.limparCronometragem) {
				await fetch('/rest/cronometragem', {
					method: 'POST',
					headers: {
						Authorization: getAuthHeader(),
						'Content-Type': 'application/json'
					},
					body: JSON.stringify({ cronometragem: [] })
				}).catch(() => undefined);
			}

			if (options.limparRfidCards) {
				await fetch('/rest/rfid', {
					method: 'POST',
					headers: {
						Authorization: getAuthHeader(),
						'Content-Type': 'application/json'
					},
					body: JSON.stringify({ cards: [] })
				}).catch(() => undefined);
			}

			if (options.limparAtletas) {
				await salvarAtletas([]).catch(() => []);
			}

			if (options.limparCategorias) {
				await salvarCategorias([]).catch(() => []);
			}

			return {
				status: 'sucesso',
				mensagem: 'Estação resetada com sucesso através de fallback de serviços.'
			};
		}

		const errBody = await response.json().catch(() => ({}));
		throw new Error(errBody?.mensagem || `Erro ${response.status}: ${response.statusText}`);
	} catch (error: any) {
		console.error('Erro ao resetar dados da estação:', error);
		throw new Error(error?.message || 'Falha ao resetar dados da estação.');
	}
}



