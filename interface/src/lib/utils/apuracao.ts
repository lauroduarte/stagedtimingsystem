import type {
	Atleta,
	Categoria,
	CriterioVoltas,
	CronometragemItem,
	Cronometrar,
	EtapaApurada,
	PilotoApurado,
	Prova,
	RelatorioResultadosPublicos,
	RFIDCard,
	SituacaoPiloto,
	StatusResultadoProva,
	SyncDiagnostico
} from '$lib/types/models';

export const MODO_ESPECIAL = 'especial';
export const MODO_LAPS = 'laps';
export const CRITERIO_VOLTAS_TEMPO: CriterioVoltas = 'voltas_tempo';
export const CRITERIO_MELHOR_VOLTA: CriterioVoltas = 'melhor_volta';

export function parseHMSParaMs(hms: string | undefined | null): number | null {
	if (!hms || hms.length < 8) return null;
	const partes = hms.trim().split(':');
	if (partes.length !== 3) return null;
	const h = Number(partes[0]);
	const m = Number(partes[1]);
	const [segStr, msRaw] = partes[2].split('.');
	const s = Number(segStr);
	if (
		!Number.isInteger(h) ||
		!Number.isInteger(m) ||
		!Number.isInteger(s) ||
		h < 0 ||
		h > 23 ||
		m < 0 ||
		m > 59 ||
		s < 0 ||
		s > 59
	) {
		return null;
	}
	let ms = 0;
	if (msRaw !== undefined && msRaw.length > 0) {
		const digits = msRaw.replace(/\D/g, '').slice(0, 3).padEnd(3, '0');
		ms = Number(digits);
		if (!Number.isInteger(ms) || ms < 0 || ms > 999) {
			ms = 0;
		}
	}
	return (h * 3600 + m * 60 + s) * 1000 + ms;
}

export function parseHMSParaSegundos(hms: string | undefined | null): number | null {
	const ms = parseHMSParaMs(hms);
	return ms === null ? null : Math.floor(ms / 1000);
}

export function formatarMsParaHMS(totalMs: number): string {
	if (typeof totalMs !== 'number' || totalMs < 0 || !Number.isFinite(totalMs)) {
		return '';
	}
	const inteiroMs = Math.round(totalMs);
	const totalSegundos = Math.floor(inteiroMs / 1000);
	const ms = inteiroMs % 1000;
	const hours = Math.floor(totalSegundos / 3600);
	const minutes = Math.floor((totalSegundos % 3600) / 60);
	const seconds = totalSegundos % 60;
	const pad2 = (n: number) => String(n).padStart(2, '0');
	const pad3 = (n: number) => String(n).padStart(3, '0');
	return `${pad2(hours)}:${pad2(minutes)}:${pad2(seconds)}.${pad3(ms)}`;
}

export function formatarSegundosParaHMS(totalSegundos: number): string {
	if (typeof totalSegundos !== 'number' || totalSegundos < 0 || !Number.isFinite(totalSegundos)) {
		return '';
	}
	return formatarMsParaHMS(Math.round(totalSegundos * 1000));
}

export function formatarGapMs(diffMs: number): string {
	if (diffMs <= 0 || !Number.isFinite(diffMs)) {
		return '+00:00:00.000';
	}
	return `+${formatarMsParaHMS(diffMs)}`;
}

export function formatarGapSegundos(diffSegundos: number): string {
	if (diffSegundos <= 0) {
		return '+00:00:00.000';
	}
	return formatarGapMs(Math.round(diffSegundos * 1000));
}

export function calcularDuracaoEntreHorarios(
	inicio: string,
	fim: string
): { segundos: number; ms: number; formatado: string } | null {
	const msIni = parseHMSParaMs(inicio);
	let msFim = parseHMSParaMs(fim);
	if (msIni === null || msFim === null) return null;
	if (msFim < msIni) {
		msFim += 86400000;
	}
	const diffMs = msFim - msIni;
	return {
		segundos: Math.floor(diffMs / 1000),
		ms: diffMs,
		formatado: formatarMsParaHMS(diffMs)
	};
}

function obterDuracaoEtapaMs(et: EtapaApurada): number {
	if (typeof et.duracaoMs === 'number' && et.duracaoMs >= 0) {
		return et.duracaoMs;
	}
	const parsed = parseHMSParaMs(et.duracao);
	if (parsed !== null && parsed >= 0) {
		return parsed;
	}
	return typeof et.duracaoSegundos === 'number' && et.duracaoSegundos >= 0
		? et.duracaoSegundos * 1000
		: -1;
}

function obterTempoTotalPilotoMs(p: PilotoApurado): number {
	if (typeof p.tempoTotalMs === 'number' && p.tempoTotalMs >= 0) {
		return p.tempoTotalMs;
	}
	const parsed = parseHMSParaMs(p.tempoTotal);
	if (parsed !== null && parsed >= 0) {
		return parsed;
	}
	return typeof p.tempoTotalSegundos === 'number' && p.tempoTotalSegundos >= 0
		? p.tempoTotalSegundos * 1000
		: -1;
}

function obterMelhorVoltaPilotoMs(p: PilotoApurado): number {
	if (typeof p.melhorVoltaMs === 'number' && p.melhorVoltaMs >= 0) {
		return p.melhorVoltaMs;
	}
	const parsed = parseHMSParaMs(p.melhorVolta);
	if (parsed !== null && parsed >= 0) {
		return parsed;
	}
	return typeof p.melhorVoltaSegundos === 'number' && p.melhorVoltaSegundos >= 0
		? p.melhorVoltaSegundos * 1000
		: -1;
}

function prioridadeSituacao(situacao: SituacaoPiloto): number {
	switch (situacao) {
		case 'CLASSIFICADO':
			return 0;
		case 'EM_CURSO':
			return 1;
		case 'DNF':
			return 2;
		default:
			return 3; // DNS
	}
}

function extrairIndiceCodigo(codigo: string, prefixo: string): number {
	if (!codigo || codigo.length < 2 || !codigo.startsWith(prefixo)) return -1;
	const n = Number(codigo.slice(prefixo.length));
	return Number.isInteger(n) && n >= 0 ? n : -1;
}

function calcularTextoGapVolta(
	piloto: PilotoApurado,
	voltasLider: number,
	tempoTotalLiderMs: number,
	melhorVoltaLiderMs: number,
	usarMelhorVolta: boolean
): string {
	if (usarMelhorVolta) {
		return formatarGapMs(obterMelhorVoltaPilotoMs(piloto) - melhorVoltaLiderMs);
	}
	const diffVoltas = voltasLider - piloto.voltasConcluidas;
	if (diffVoltas <= 0) {
		return formatarGapMs(obterTempoTotalPilotoMs(piloto) - tempoTotalLiderMs);
	}
	return `-${diffVoltas} ${diffVoltas === 1 ? 'volta' : 'voltas'}`;
}

export function reordenarClassificacaoPorCriterio(
	listaOriginal: PilotoApurado[],
	modoProva: string,
	criterioVoltas: CriterioVoltas
): PilotoApurado[] {
	const copia: PilotoApurado[] = listaOriginal.map((p) => ({
		...p,
		tempoTotalMs: obterTempoTotalPilotoMs(p),
		melhorVoltaMs: obterMelhorVoltaPilotoMs(p),
		etapas: p.etapas
			? p.etapas.map((e) => ({
					...e,
					duracaoMs: obterDuracaoEtapaMs(e)
				}))
			: []
	}));

	if (modoProva === MODO_ESPECIAL) {
		copia.sort((a, b) => {
			const prioA = prioridadeSituacao(a.situacao);
			const prioB = prioridadeSituacao(b.situacao);
			if (prioA !== prioB) return prioA - prioB;

			const totalA = obterTempoTotalPilotoMs(a);
			const totalB = obterTempoTotalPilotoMs(b);

			if (a.situacao === 'CLASSIFICADO') {
				if (totalA !== totalB) {
					return totalA - totalB;
				}
				const len = Math.min(a.etapas.length, b.etapas.length);
				for (let i = len - 1; i >= 0; i--) {
					const ta = obterDuracaoEtapaMs(a.etapas[i]);
					const tb = obterDuracaoEtapaMs(b.etapas[i]);
					if (ta >= 0 && tb >= 0 && ta !== tb) {
						return ta - tb;
					}
				}
				return a.numeral - b.numeral;
			}

			if (a.situacao === 'EM_CURSO' || a.situacao === 'DNF') {
				if (a.etapasConcluidas !== b.etapasConcluidas) {
					return b.etapasConcluidas - a.etapasConcluidas;
				}
				if (totalA >= 0 && totalB >= 0 && totalA !== totalB) {
					return totalA - totalB;
				}
				return a.numeral - b.numeral;
			}

			return a.numeral - b.numeral;
		});

		let posGeral = 1;
		let liderGeralMs = -1;
		const posPorCat = new Map<number, number>();
		const liderPorCatMs = new Map<number, number>();

		for (const p of copia) {
			if (p.situacao === 'CLASSIFICADO') {
				const totalMs = obterTempoTotalPilotoMs(p);
				p.posicaoGeral = posGeral++;
				if (liderGeralMs < 0) {
					liderGeralMs = totalMs;
				}
				p.gapGeral = formatarGapMs(totalMs - liderGeralMs);

				const catPos = (posPorCat.get(p.categoriaId) ?? 0) + 1;
				posPorCat.set(p.categoriaId, catPos);
				p.posicaoCategoria = catPos;

				if (!liderPorCatMs.has(p.categoriaId)) {
					liderPorCatMs.set(p.categoriaId, totalMs);
				}
				const liderCatMs = liderPorCatMs.get(p.categoriaId) ?? totalMs;
				p.gapCategoria = formatarGapMs(totalMs - liderCatMs);
			} else {
				p.posicaoGeral = 0;
				p.posicaoCategoria = 0;
				p.gapGeral = p.situacao === 'DNS' ? 'DNS' : p.situacao === 'EM_CURSO' ? 'EM CURSO' : 'DNF';
				p.gapCategoria = p.gapGeral;
			}
		}

		return copia;
	}

	// Modo Voltas (laps)
	const usarMelhorVolta = criterioVoltas === CRITERIO_MELHOR_VOLTA;

	const obterUltimaVoltaMs = (p: PilotoApurado): number => {
		for (let i = p.etapas.length - 1; i >= 0; i--) {
			const durMs = obterDuracaoEtapaMs(p.etapas[i]);
			if (p.etapas[i].status === 'OK' && durMs >= 0) {
				return durMs;
			}
		}
		return -1;
	};

	copia.sort((a, b) => {
		const prioA = prioridadeSituacao(a.situacao);
		const prioB = prioridadeSituacao(b.situacao);
		if (prioA !== prioB) return prioA - prioB;

		if (a.situacao === 'CLASSIFICADO') {
			const melhorA = obterMelhorVoltaPilotoMs(a);
			const melhorB = obterMelhorVoltaPilotoMs(b);
			const totalA = obterTempoTotalPilotoMs(a);
			const totalB = obterTempoTotalPilotoMs(b);

			if (usarMelhorVolta) {
				if (melhorA !== melhorB) {
					return melhorA - melhorB;
				}
				if (a.voltasConcluidas !== b.voltasConcluidas) {
					return b.voltasConcluidas - a.voltasConcluidas;
				}
				if (totalA !== totalB) {
					return totalA - totalB;
				}
			} else {
				if (a.voltasConcluidas !== b.voltasConcluidas) {
					return b.voltasConcluidas - a.voltasConcluidas;
				}
				if (totalA !== totalB) {
					return totalA - totalB;
				}
				const ultA = obterUltimaVoltaMs(a);
				const ultB = obterUltimaVoltaMs(b);
				if (ultA >= 0 && ultB >= 0 && ultA !== ultB) {
					return ultA - ultB;
				}
				if (melhorA >= 0 && melhorB >= 0 && melhorA !== melhorB) {
					return melhorA - melhorB;
				}
			}
			return a.numeral - b.numeral;
		}

		return a.numeral - b.numeral;
	});

	let posGeral = 1;
	let voltasLiderGeral = -1;
	let tempoLiderGeralMs = -1;
	let melhorVoltaLiderGeralMs = -1;

	const posPorCat = new Map<number, number>();
	const voltasLiderCat = new Map<number, number>();
	const tempoLiderCatMs = new Map<number, number>();
	const melhorVoltaLiderCatMs = new Map<number, number>();

	for (const p of copia) {
		if (p.situacao === 'CLASSIFICADO') {
			const totalMs = obterTempoTotalPilotoMs(p);
			const melhorMs = obterMelhorVoltaPilotoMs(p);
			p.posicaoGeral = posGeral++;
			if (voltasLiderGeral < 0) {
				voltasLiderGeral = p.voltasConcluidas;
				tempoLiderGeralMs = totalMs;
				melhorVoltaLiderGeralMs = melhorMs;
			}
			p.gapGeral = calcularTextoGapVolta(
				p,
				voltasLiderGeral,
				tempoLiderGeralMs,
				melhorVoltaLiderGeralMs,
				usarMelhorVolta
			);

			const catPos = (posPorCat.get(p.categoriaId) ?? 0) + 1;
			posPorCat.set(p.categoriaId, catPos);
			p.posicaoCategoria = catPos;

			if (!voltasLiderCat.has(p.categoriaId)) {
				voltasLiderCat.set(p.categoriaId, p.voltasConcluidas);
				tempoLiderCatMs.set(p.categoriaId, totalMs);
				melhorVoltaLiderCatMs.set(p.categoriaId, melhorMs);
			}
			p.gapCategoria = calcularTextoGapVolta(
				p,
				voltasLiderCat.get(p.categoriaId) ?? p.voltasConcluidas,
				tempoLiderCatMs.get(p.categoriaId) ?? totalMs,
				melhorVoltaLiderCatMs.get(p.categoriaId) ?? melhorMs,
				usarMelhorVolta
			);
		} else {
			p.posicaoGeral = 0;
			p.posicaoCategoria = 0;
			p.gapGeral = p.situacao === 'DNS' ? 'DNS' : p.situacao === 'EM_CURSO' ? 'EM CURSO' : 'DNF';
			p.gapCategoria = p.gapGeral;
		}
	}

	return copia;
}

export function apurarResultadosLocais(params: {
	prova?: Prova;
	categorias: Categoria[];
	atletas: Atleta[];
	rfidCards: RFIDCard[];
	estacao?: Cronometrar;
	cronometragens: CronometragemItem[];
	criterioVoltasOverride?: CriterioVoltas;
}): RelatorioResultadosPublicos {
	const {
		prova,
		categorias,
		atletas,
		rfidCards,
		estacao,
		cronometragens,
		criterioVoltasOverride
	} = params;

	let modoProva = prova?.tipo || estacao?.tipo || MODO_ESPECIAL;
	if (modoProva !== MODO_ESPECIAL && modoProva !== MODO_LAPS) {
		modoProva = MODO_ESPECIAL;
	}
	if (!prova?.nome && cronometragens.length > 0) {
		const temLap = cronometragens.some(
			(r) => r.tipo === MODO_LAPS || (r.especial && r.especial.startsWith('L'))
		);
		const temEspecial = cronometragens.some((r) => r.especial && r.especial.startsWith('E'));
		if (temLap && !temEspecial) {
			modoProva = MODO_LAPS;
		}
	}

	const modoVoltas =
		prova?.modoVoltas === 'duas' || estacao?.modoVoltas === 'duas' ? 'duas' : 'unica';
	const criterioRaw =
		criterioVoltasOverride ||
		(prova?.criterioVoltas === CRITERIO_MELHOR_VOLTA
			? CRITERIO_MELHOR_VOLTA
			: CRITERIO_VOLTAS_TEMPO);
	const criterioVoltas: CriterioVoltas =
		criterioRaw === CRITERIO_MELHOR_VOLTA ? CRITERIO_MELHOR_VOLTA : CRITERIO_VOLTAS_TEMPO;

	const totalEstagiosConfigurados =
		prova?.totalEstagios && prova.totalEstagios >= 1 && prova.totalEstagios <= 10
			? prova.totalEstagios
			: 5;

	const mapaCategorias = new Map<number, string>();
	for (const c of categorias) {
		mapaCategorias.set(c.id, c.categoria);
	}

	const uidParaNumeral = new Map<string, number>();
	const numeralParaUid = new Map<number, string>();
	for (const card of rfidCards) {
		if (card.uid && card.numero > 0) {
			uidParaNumeral.set(card.uid, card.numero);
			numeralParaUid.set(card.numero, card.uid);
		}
	}

	// Normaliza registros
	const registros: CronometragemItem[] = cronometragens.map((raw) => {
		const item: CronometragemItem = { ...raw };
		if ((!item.numeral || item.numeral <= 0) && item.uid) {
			const n = uidParaNumeral.get(item.uid);
			if (n) item.numeral = n;
		} else if (!item.uid && item.numeral && item.numeral > 0) {
			const u = numeralParaUid.get(item.numeral);
			if (u) item.uid = u;
		}

		if (item.inicio && item.fim) {
			const calc = calcularDuracaoEntreHorarios(item.inicio, item.fim);
			if (calc) {
				item.duracaoSegundos = calc.segundos;
				item.duracaoMs = calc.ms;
				item.duracao = calc.formatado;
				item.status = 'OK';
				item.incompleto = false;
			} else {
				item.duracaoSegundos = -1;
				item.duracaoMs = -1;
				item.duracao = '';
				item.status = 'invalido';
				item.incompleto = true;
			}
		} else if (!item.inicio && item.fim) {
			item.duracaoSegundos = -1;
			item.duracaoMs = -1;
			item.duracao = '';
			item.status = 'sem_largada';
			item.incompleto = true;
		} else if (item.inicio && !item.fim) {
			item.duracaoSegundos = -1;
			item.duracaoMs = -1;
			item.duracao = '';
			item.status = 'largada';
			item.incompleto = false;
		}
		return item;
	});

	const diagnostico: SyncDiagnostico = {
		totalRegistros: registros.length,
		concluidos: 0,
		pendentesChegada: 0,
		semLargada: 0,
		invalidos: 0
	};

	for (const r of registros) {
		if (r.status === 'OK') diagnostico.concluidos++;
		else if (r.status === 'sem_largada') diagnostico.semLargada++;
		else if (r.status === 'invalido') diagnostico.invalidos++;
		else if (r.status === 'largada' && r.especial !== 'L0') diagnostico.pendentesChegada++;
	}

	const cronometrandoAtivo = Boolean(estacao?.cronometrar);
	let statusResultado: StatusResultadoProva = 'aguardando';
	let resultadoOficial = false;

	if (diagnostico.totalRegistros === 0) {
		statusResultado = 'aguardando';
		resultadoOficial = false;
	} else if (cronometrandoAtivo || diagnostico.pendentesChegada > 0 || diagnostico.concluidos === 0) {
		statusResultado = 'parcial';
		resultadoOficial = false;
	} else {
		statusResultado = 'oficial';
		resultadoOficial = true;
	}

	const pilotos: PilotoApurado[] = [];
	const idxPorNumeral = new Map<number, number>();
	const idxPorUid = new Map<string, number>();

	for (const atl of atletas) {
		const uid = numeralParaUid.get(atl.numeral) || '';
		const p: PilotoApurado = {
			numeral: atl.numeral,
			nome: atl.nome,
			equipe: atl.equipe,
			categoriaId: atl.categoriaId,
			categoriaNome: mapaCategorias.get(atl.categoriaId) || 'Sem Categoria',
			uid,
			situacao: 'DNS',
			etapasConcluidas: 0,
			etapasEsperadas: 0,
			voltasConcluidas: 0,
			tempoTotalSegundos: -1,
			tempoTotalMs: -1,
			tempoTotal: '',
			melhorVoltaSegundos: -1,
			melhorVoltaMs: -1,
			melhorVolta: '',
			posicaoGeral: 0,
			gapGeral: 'DNS',
			posicaoCategoria: 0,
			gapCategoria: 'DNS',
			horarioLargadaInicial: '',
			etapas: []
		};
		const idx = pilotos.length;
		pilotos.push(p);
		if (p.numeral > 0) idxPorNumeral.set(p.numeral, idx);
		if (p.uid) idxPorUid.set(p.uid, idx);
	}

	for (const r of registros) {
		const num = r.numeral ?? 0;
		const jaTem = (num > 0 && idxPorNumeral.has(num)) || (r.uid && idxPorUid.has(r.uid));
		if (!jaTem) {
			const avulso: PilotoApurado = {
				numeral: num,
				nome: num > 0 ? `Piloto #${num}` : `Cartão ${r.uid}`,
				equipe: 'Avulso',
				categoriaId: 0,
				categoriaNome: 'Sem Categoria',
				uid: r.uid || '',
				situacao: 'DNS',
				etapasConcluidas: 0,
				etapasEsperadas: 0,
				voltasConcluidas: 0,
				tempoTotalSegundos: -1,
				tempoTotalMs: -1,
				tempoTotal: '',
				melhorVoltaSegundos: -1,
				melhorVoltaMs: -1,
				melhorVolta: '',
				posicaoGeral: 0,
				gapGeral: 'DNS',
				posicaoCategoria: 0,
				gapCategoria: 'DNS',
				horarioLargadaInicial: '',
				etapas: []
			};
			const idx = pilotos.length;
			pilotos.push(avulso);
			if (avulso.numeral > 0) idxPorNumeral.set(avulso.numeral, idx);
			if (avulso.uid) idxPorUid.set(avulso.uid, idx);
		}
	}

	const localizarPilotoIdx = (r: CronometragemItem): number | undefined => {
		if (r.numeral && r.numeral > 0 && idxPorNumeral.has(r.numeral)) {
			return idxPorNumeral.get(r.numeral);
		}
		if (r.uid && idxPorUid.has(r.uid)) {
			return idxPorUid.get(r.uid);
		}
		return undefined;
	};

	const codigosEtapas: string[] = [];
	let totalEtapasDisputadas = 0;

	if (modoProva === MODO_ESPECIAL) {
		let maxEstagioComRegistro = 0;
		for (const r of registros) {
			const idx = extrairIndiceCodigo(r.especial, 'E');
			if (idx > maxEstagioComRegistro && idx <= 10) {
				maxEstagioComRegistro = idx;
			}
		}

		const etapasDisputadas =
			maxEstagioComRegistro > 0 ? maxEstagioComRegistro : totalEstagiosConfigurados;
		const etapasExibidas = Math.max(totalEstagiosConfigurados, maxEstagioComRegistro);
		totalEtapasDisputadas = maxEstagioComRegistro;

		for (let i = 1; i <= etapasExibidas; i++) {
			codigosEtapas.push(`E${i}`);
		}

		for (const p of pilotos) {
			p.etapasEsperadas = etapasDisputadas;
			p.etapas = codigosEtapas.map((cod, i) => ({
				codigo: cod,
				indice: i + 1,
				inicio: '',
				fim: '',
				duracao: '',
				duracaoSegundos: -1,
				duracaoMs: -1,
				status: 'pendente',
				posicaoEtapa: 0,
				gapEtapa: ''
			}));
		}

		for (const r of registros) {
			const stageNum = extrairIndiceCodigo(r.especial, 'E');
			if (stageNum < 1 || stageNum > etapasExibidas) continue;
			const pIdx = localizarPilotoIdx(r);
			if (pIdx === undefined) continue;

			const et = pilotos[pIdx].etapas[stageNum - 1];
			et.inicio = r.inicio || '';
			et.fim = r.fim || '';
			et.duracao = r.duracao || '';
			et.duracaoSegundos = typeof r.duracaoSegundos === 'number' ? r.duracaoSegundos : -1;
			et.duracaoMs =
				typeof r.duracaoMs === 'number'
					? r.duracaoMs
					: et.duracaoSegundos >= 0
						? et.duracaoSegundos * 1000
						: -1;
			et.status = r.status || 'pendente';
		}

		calcularPosicoesEGapsPorEtapaTS(pilotos, etapasExibidas);

		for (const p of pilotos) {
			let concluidasDisputadas = 0;
			let concluidasTotais = 0;
			let temAlgumRegistro = false;
			let temFalhaOuIncompleto = false;
			let temLargadaAberta = false;
			let somaMs = 0;

			for (let i = 0; i < etapasExibidas; i++) {
				const et = p.etapas[i];
				if (et.status !== 'pendente') temAlgumRegistro = true;
				const durMs = obterDuracaoEtapaMs(et);
				if (et.status === 'OK' && durMs >= 0) {
					concluidasTotais++;
					if (i < etapasDisputadas) concluidasDisputadas++;
					somaMs += durMs;
				} else if (et.status === 'sem_largada' || et.status === 'invalido') {
					temFalhaOuIncompleto = true;
				} else if (et.status === 'largada') {
					temLargadaAberta = true;
				}
			}

			p.etapasConcluidas = concluidasTotais;

			if (!temAlgumRegistro) {
				p.situacao = 'DNS';
				p.tempoTotalSegundos = -1;
				p.tempoTotalMs = -1;
				p.tempoTotal = '';
			} else if (
				maxEstagioComRegistro > 0 &&
				concluidasDisputadas === etapasDisputadas &&
				!temFalhaOuIncompleto &&
				!temLargadaAberta
			) {
				p.situacao = 'CLASSIFICADO';
				p.tempoTotalMs = somaMs;
				p.tempoTotalSegundos = Math.floor(somaMs / 1000);
				p.tempoTotal = formatarMsParaHMS(somaMs);
			} else {
				p.situacao = cronometrandoAtivo && temLargadaAberta && !temFalhaOuIncompleto ? 'EM_CURSO' : 'DNF';
				p.tempoTotalMs = concluidasTotais > 0 ? somaMs : -1;
				p.tempoTotalSegundos = concluidasTotais > 0 ? Math.floor(somaMs / 1000) : -1;
				p.tempoTotal = concluidasTotais > 0 ? formatarMsParaHMS(somaMs) : '';
			}
		}
	} else {
		// Modo Voltas
		let maxVolta = 0;
		for (const r of registros) {
			let v = r.volta ?? 0;
			if (v <= 0 && r.especial && r.especial.startsWith('L')) {
				v = extrairIndiceCodigo(r.especial, 'L');
			}
			if (v > maxVolta) maxVolta = v;
		}

		totalEtapasDisputadas = maxVolta;
		for (let v = 1; v <= maxVolta; v++) {
			codigosEtapas.push(`L${v}`);
		}

		for (const p of pilotos) {
			p.etapasEsperadas = maxVolta;
			p.etapas = codigosEtapas.map((cod, i) => ({
				codigo: cod,
				indice: i + 1,
				inicio: '',
				fim: '',
				duracao: '',
				duracaoSegundos: -1,
				duracaoMs: -1,
				status: 'pendente',
				posicaoEtapa: 0,
				gapEtapa: ''
			}));
		}

		const temRegistroPorPiloto = new Set<number>();
		for (const r of registros) {
			const pIdx = localizarPilotoIdx(r);
			if (pIdx === undefined) continue;
			temRegistroPorPiloto.add(pIdx);

			let v = r.volta ?? 0;
			if (v <= 0 && r.especial && r.especial.startsWith('L')) {
				v = extrairIndiceCodigo(r.especial, 'L');
			}

			if (v === 0 || r.especial === 'L0') {
				pilotos[pIdx].horarioLargadaInicial = r.inicio || '';
				continue;
			}

			if (v >= 1 && v <= maxVolta) {
				const et = pilotos[pIdx].etapas[v - 1];
				et.inicio = r.inicio || '';
				et.fim = r.fim || '';
				et.duracao = r.duracao || '';
				et.duracaoSegundos = typeof r.duracaoSegundos === 'number' ? r.duracaoSegundos : -1;
				et.duracaoMs =
					typeof r.duracaoMs === 'number'
						? r.duracaoMs
						: et.duracaoSegundos >= 0
							? et.duracaoSegundos * 1000
							: -1;
				et.status = r.status || 'pendente';
			}
		}

		calcularPosicoesEGapsPorEtapaTS(pilotos, maxVolta);

		pilotos.forEach((p, idx) => {
			let voltasOk = 0;
			let somaMs = 0;
			let melhorVoltaMs = -1;
			let temLargadaAberta = Boolean(p.horarioLargadaInicial);

			for (const et of p.etapas) {
				const durMs = obterDuracaoEtapaMs(et);
				if (et.status === 'OK' && durMs >= 0) {
					voltasOk++;
					somaMs += durMs;
					if (melhorVoltaMs < 0 || durMs < melhorVoltaMs) {
						melhorVoltaMs = durMs;
					}
				} else if (et.status === 'largada') {
					temLargadaAberta = true;
				}
			}

			p.voltasConcluidas = voltasOk;
			p.etapasConcluidas = voltasOk;

			if (!temRegistroPorPiloto.has(idx)) {
				p.situacao = 'DNS';
				p.tempoTotalSegundos = -1;
				p.tempoTotalMs = -1;
				p.tempoTotal = '';
				p.melhorVoltaSegundos = -1;
				p.melhorVoltaMs = -1;
				p.melhorVolta = '';
			} else if (voltasOk > 0) {
				p.situacao = 'CLASSIFICADO';
				p.tempoTotalMs = somaMs;
				p.tempoTotalSegundos = Math.floor(somaMs / 1000);
				p.tempoTotal = formatarMsParaHMS(somaMs);
				p.melhorVoltaMs = melhorVoltaMs;
				p.melhorVoltaSegundos = Math.floor(melhorVoltaMs / 1000);
				p.melhorVolta = formatarMsParaHMS(melhorVoltaMs);
			} else {
				p.situacao = cronometrandoAtivo && temLargadaAberta ? 'EM_CURSO' : 'DNF';
				p.tempoTotalSegundos = -1;
				p.tempoTotalMs = -1;
				p.tempoTotal = '';
				p.melhorVoltaSegundos = -1;
				p.melhorVoltaMs = -1;
				p.melhorVolta = '';
			}
		});
	}

	const classificacaoOrdenada = reordenarClassificacaoPorCriterio(
		pilotos,
		modoProva,
		criterioVoltas
	);

	return {
		versao: 1,
		resultadoOficial,
		statusResultado,
		modoProva,
		modoVoltas,
		criterioVoltas,
		totalEstagiosConfigurados,
		totalEtapasDisputadas,
		prova,
		estacao,
		diagnostico,
		codigosEtapas,
		categorias,
		classificacao: classificacaoOrdenada,
		cronometragem: registros
	};
}

function calcularPosicoesEGapsPorEtapaTS(pilotos: PilotoApurado[], qtdEtapas: number) {
	for (let eIdx = 0; eIdx < qtdEtapas; eIdx++) {
		const concluintes: PilotoApurado[] = [];
		for (const p of pilotos) {
			const et: EtapaApurada | undefined = p.etapas[eIdx];
			if (et && et.status === 'OK' && obterDuracaoEtapaMs(et) >= 0) {
				concluintes.push(p);
			}
		}

		concluintes.sort((a, b) => {
			const ta = obterDuracaoEtapaMs(a.etapas[eIdx]);
			const tb = obterDuracaoEtapaMs(b.etapas[eIdx]);
			if (ta !== tb) return ta - tb;
			return a.numeral - b.numeral;
		});

		if (concluintes.length > 0) {
			const melhorMs = obterDuracaoEtapaMs(concluintes[0].etapas[eIdx]);
			concluintes.forEach((p, rank) => {
				p.etapas[eIdx].posicaoEtapa = rank + 1;
				const durMs = obterDuracaoEtapaMs(p.etapas[eIdx]);
				p.etapas[eIdx].gapEtapa = formatarGapMs(durMs - melhorMs);
			});
		}
	}
}

export function gerarCsvClassificacaoOficial(
	relatorio: RelatorioResultadosPublicos,
	filtroCategoriaId: number | null = null
): string {
	const isLaps = relatorio.modoProva === MODO_LAPS;
	const etapasCols = relatorio.codigosEtapas || [];

	const headers = [
		'Pos_Geral',
		'Pos_Categoria',
		'Situacao',
		'Numeral',
		'Piloto',
		'Equipe',
		'Categoria',
		...(isLaps
			? ['Voltas_Concluidas', 'Tempo_Total', 'Melhor_Volta', 'Gap_Geral', 'Gap_Categoria']
			: ['Etapas_Concluidas', 'Tempo_Total', 'Gap_Geral', 'Gap_Categoria']),
		...etapasCols.map((cod) => `${cod}_Tempo`)
	];

	const escaparCsv = (val: string | number | undefined | null): string => {
		const str = String(val ?? '').replace(/"/g, '""');
		return str.includes(';') || str.includes('"') || str.includes('\n') ? `"${str}"` : str;
	};

	const filtrados =
		filtroCategoriaId !== null
			? relatorio.classificacao.filter((p) => p.categoriaId === filtroCategoriaId)
			: relatorio.classificacao;

	const rows = filtrados.map((p) => {
		const posGeralStr = p.posicaoGeral > 0 ? `${p.posicaoGeral}` : p.situacao;
		const posCatStr = p.posicaoCategoria > 0 ? `${p.posicaoCategoria}` : p.situacao;
		const temposEtapas = etapasCols.map((cod) => {
			const et = p.etapas.find((e) => e.codigo === cod);
			if (!et) return '';
			if (et.status === 'OK') return et.duracao;
			if (et.status === 'largada') return 'EM_CURSO';
			if (et.status === 'sem_largada') return 'SEM_LARGADA';
			return '';
		});

		const baseCols = [
			posGeralStr,
			posCatStr,
			p.situacao,
			p.numeral > 0 ? p.numeral : '',
			p.nome,
			p.equipe,
			p.categoriaNome,
			...(isLaps
				? [
						p.voltasConcluidas,
						p.tempoTotal || '—',
						p.melhorVolta || '—',
						p.gapGeral || '—',
						p.gapCategoria || '—'
					]
				: [
						`${p.etapasConcluidas}/${p.etapasEsperadas}`,
						p.tempoTotal || '—',
						p.gapGeral || '—',
						p.gapCategoria || '—'
					]),
			...temposEtapas
		];

		return baseCols.map(escaparCsv);
	});

	return [headers.join(';'), ...rows.map((r) => r.join(';'))].join('\r\n');
}

