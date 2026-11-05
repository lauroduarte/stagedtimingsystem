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

export function parseHMSParaSegundos(hms: string | undefined | null): number | null {
	if (!hms || hms.length < 8) return null;
	const parts = hms.trim().split(':');
	if (parts.length !== 3) return null;
	const h = Number(parts[0]);
	const m = Number(parts[1]);
	const s = Number(parts[2]);
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
	return h * 3600 + m * 60 + s;
}

export function formatarSegundosParaHMS(totalSegundos: number): string {
	if (typeof totalSegundos !== 'number' || totalSegundos < 0 || !Number.isFinite(totalSegundos)) {
		return '';
	}
	const inteiro = Math.floor(totalSegundos);
	const hours = Math.floor(inteiro / 3600);
	const minutes = Math.floor((inteiro % 3600) / 60);
	const seconds = inteiro % 60;
	const pad = (n: number) => String(n).padStart(2, '0');
	return `${pad(hours)}:${pad(minutes)}:${pad(seconds)}`;
}

export function formatarGapSegundos(diffSegundos: number): string {
	if (diffSegundos <= 0) {
		return '+00:00:00';
	}
	return `+${formatarSegundosParaHMS(diffSegundos)}`;
}

export function calcularDuracaoEntreHorarios(
	inicio: string,
	fim: string
): { segundos: number; formatado: string } | null {
	const secIni = parseHMSParaSegundos(inicio);
	let secFim = parseHMSParaSegundos(fim);
	if (secIni === null || secFim === null) return null;
	if (secFim < secIni) {
		secFim += 86400;
	}
	const diff = secFim - secIni;
	return {
		segundos: diff,
		formatado: formatarSegundosParaHMS(diff)
	};
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
	tempoTotalLider: number,
	melhorVoltaLider: number,
	usarMelhorVolta: boolean
): string {
	if (usarMelhorVolta) {
		return formatarGapSegundos(piloto.melhorVoltaSegundos - melhorVoltaLider);
	}
	const diffVoltas = voltasLider - piloto.voltasConcluidas;
	if (diffVoltas <= 0) {
		return formatarGapSegundos(piloto.tempoTotalSegundos - tempoTotalLider);
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
		etapas: p.etapas ? p.etapas.map((e) => ({ ...e })) : []
	}));

	if (modoProva === MODO_ESPECIAL) {
		copia.sort((a, b) => {
			const prioA = prioridadeSituacao(a.situacao);
			const prioB = prioridadeSituacao(b.situacao);
			if (prioA !== prioB) return prioA - prioB;

			if (a.situacao === 'CLASSIFICADO') {
				if (a.tempoTotalSegundos !== b.tempoTotalSegundos) {
					return a.tempoTotalSegundos - b.tempoTotalSegundos;
				}
				const len = Math.min(a.etapas.length, b.etapas.length);
				for (let i = len - 1; i >= 0; i--) {
					const ta = a.etapas[i].duracaoSegundos;
					const tb = b.etapas[i].duracaoSegundos;
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
				if (
					a.tempoTotalSegundos >= 0 &&
					b.tempoTotalSegundos >= 0 &&
					a.tempoTotalSegundos !== b.tempoTotalSegundos
				) {
					return a.tempoTotalSegundos - b.tempoTotalSegundos;
				}
				return a.numeral - b.numeral;
			}

			return a.numeral - b.numeral;
		});

		let posGeral = 1;
		let liderGeralSeg = -1;
		const posPorCat = new Map<number, number>();
		const liderPorCat = new Map<number, number>();

		for (const p of copia) {
			if (p.situacao === 'CLASSIFICADO') {
				p.posicaoGeral = posGeral++;
				if (liderGeralSeg < 0) {
					liderGeralSeg = p.tempoTotalSegundos;
				}
				p.gapGeral = formatarGapSegundos(p.tempoTotalSegundos - liderGeralSeg);

				const catPos = (posPorCat.get(p.categoriaId) ?? 0) + 1;
				posPorCat.set(p.categoriaId, catPos);
				p.posicaoCategoria = catPos;

				if (!liderPorCat.has(p.categoriaId)) {
					liderPorCat.set(p.categoriaId, p.tempoTotalSegundos);
				}
				const liderCatSeg = liderPorCat.get(p.categoriaId) ?? p.tempoTotalSegundos;
				p.gapCategoria = formatarGapSegundos(p.tempoTotalSegundos - liderCatSeg);
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

	const obterUltimaVoltaSeg = (p: PilotoApurado): number => {
		for (let i = p.etapas.length - 1; i >= 0; i--) {
			if (p.etapas[i].status === 'OK' && p.etapas[i].duracaoSegundos >= 0) {
				return p.etapas[i].duracaoSegundos;
			}
		}
		return -1;
	};

	copia.sort((a, b) => {
		const prioA = prioridadeSituacao(a.situacao);
		const prioB = prioridadeSituacao(b.situacao);
		if (prioA !== prioB) return prioA - prioB;

		if (a.situacao === 'CLASSIFICADO') {
			if (usarMelhorVolta) {
				if (a.melhorVoltaSegundos !== b.melhorVoltaSegundos) {
					return a.melhorVoltaSegundos - b.melhorVoltaSegundos;
				}
				if (a.voltasConcluidas !== b.voltasConcluidas) {
					return b.voltasConcluidas - a.voltasConcluidas;
				}
				if (a.tempoTotalSegundos !== b.tempoTotalSegundos) {
					return a.tempoTotalSegundos - b.tempoTotalSegundos;
				}
			} else {
				if (a.voltasConcluidas !== b.voltasConcluidas) {
					return b.voltasConcluidas - a.voltasConcluidas;
				}
				if (a.tempoTotalSegundos !== b.tempoTotalSegundos) {
					return a.tempoTotalSegundos - b.tempoTotalSegundos;
				}
				const ultA = obterUltimaVoltaSeg(a);
				const ultB = obterUltimaVoltaSeg(b);
				if (ultA >= 0 && ultB >= 0 && ultA !== ultB) {
					return ultA - ultB;
				}
				if (
					a.melhorVoltaSegundos >= 0 &&
					b.melhorVoltaSegundos >= 0 &&
					a.melhorVoltaSegundos !== b.melhorVoltaSegundos
				) {
					return a.melhorVoltaSegundos - b.melhorVoltaSegundos;
				}
			}
			return a.numeral - b.numeral;
		}

		return a.numeral - b.numeral;
	});

	let posGeral = 1;
	let voltasLiderGeral = -1;
	let tempoLiderGeral = -1;
	let melhorVoltaLiderGeral = -1;

	const posPorCat = new Map<number, number>();
	const voltasLiderCat = new Map<number, number>();
	const tempoLiderCat = new Map<number, number>();
	const melhorVoltaLiderCat = new Map<number, number>();

	for (const p of copia) {
		if (p.situacao === 'CLASSIFICADO') {
			p.posicaoGeral = posGeral++;
			if (voltasLiderGeral < 0) {
				voltasLiderGeral = p.voltasConcluidas;
				tempoLiderGeral = p.tempoTotalSegundos;
				melhorVoltaLiderGeral = p.melhorVoltaSegundos;
			}
			p.gapGeral = calcularTextoGapVolta(
				p,
				voltasLiderGeral,
				tempoLiderGeral,
				melhorVoltaLiderGeral,
				usarMelhorVolta
			);

			const catPos = (posPorCat.get(p.categoriaId) ?? 0) + 1;
			posPorCat.set(p.categoriaId, catPos);
			p.posicaoCategoria = catPos;

			if (!voltasLiderCat.has(p.categoriaId)) {
				voltasLiderCat.set(p.categoriaId, p.voltasConcluidas);
				tempoLiderCat.set(p.categoriaId, p.tempoTotalSegundos);
				melhorVoltaLiderCat.set(p.categoriaId, p.melhorVoltaSegundos);
			}
			p.gapCategoria = calcularTextoGapVolta(
				p,
				voltasLiderCat.get(p.categoriaId) ?? p.voltasConcluidas,
				tempoLiderCat.get(p.categoriaId) ?? p.tempoTotalSegundos,
				melhorVoltaLiderCat.get(p.categoriaId) ?? p.melhorVoltaSegundos,
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
				item.duracao = calc.formatado;
				item.status = 'OK';
				item.incompleto = false;
			} else {
				item.duracaoSegundos = -1;
				item.duracao = '';
				item.status = 'invalido';
				item.incompleto = true;
			}
		} else if (!item.inicio && item.fim) {
			item.duracaoSegundos = -1;
			item.duracao = '';
			item.status = 'sem_largada';
			item.incompleto = true;
		} else if (item.inicio && !item.fim) {
			item.duracaoSegundos = -1;
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
			tempoTotal: '',
			melhorVoltaSegundos: -1,
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
				tempoTotal: '',
				melhorVoltaSegundos: -1,
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
			et.status = r.status || 'pendente';
		}

		calcularPosicoesEGapsPorEtapaTS(pilotos, etapasExibidas);

		for (const p of pilotos) {
			let concluidasDisputadas = 0;
			let concluidasTotais = 0;
			let temAlgumRegistro = false;
			let temFalhaOuIncompleto = false;
			let temLargadaAberta = false;
			let somaSegundos = 0;

			for (let i = 0; i < etapasExibidas; i++) {
				const et = p.etapas[i];
				if (et.status !== 'pendente') temAlgumRegistro = true;
				if (et.status === 'OK' && et.duracaoSegundos >= 0) {
					concluidasTotais++;
					if (i < etapasDisputadas) concluidasDisputadas++;
					somaSegundos += et.duracaoSegundos;
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
				p.tempoTotal = '';
			} else if (
				maxEstagioComRegistro > 0 &&
				concluidasDisputadas === etapasDisputadas &&
				!temFalhaOuIncompleto &&
				!temLargadaAberta
			) {
				p.situacao = 'CLASSIFICADO';
				p.tempoTotalSegundos = somaSegundos;
				p.tempoTotal = formatarSegundosParaHMS(somaSegundos);
			} else {
				p.situacao = cronometrandoAtivo && temLargadaAberta && !temFalhaOuIncompleto ? 'EM_CURSO' : 'DNF';
				p.tempoTotalSegundos = concluidasTotais > 0 ? somaSegundos : -1;
				p.tempoTotal = concluidasTotais > 0 ? formatarSegundosParaHMS(somaSegundos) : '';
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
				et.status = r.status || 'pendente';
			}
		}

		calcularPosicoesEGapsPorEtapaTS(pilotos, maxVolta);

		pilotos.forEach((p, idx) => {
			let voltasOk = 0;
			let somaSegundos = 0;
			let melhorVoltaSeg = -1;
			let temLargadaAberta = Boolean(p.horarioLargadaInicial);

			for (const et of p.etapas) {
				if (et.status === 'OK' && et.duracaoSegundos >= 0) {
					voltasOk++;
					somaSegundos += et.duracaoSegundos;
					if (melhorVoltaSeg < 0 || et.duracaoSegundos < melhorVoltaSeg) {
						melhorVoltaSeg = et.duracaoSegundos;
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
				p.tempoTotal = '';
				p.melhorVoltaSegundos = -1;
				p.melhorVolta = '';
			} else if (voltasOk > 0) {
				p.situacao = 'CLASSIFICADO';
				p.tempoTotalSegundos = somaSegundos;
				p.tempoTotal = formatarSegundosParaHMS(somaSegundos);
				p.melhorVoltaSegundos = melhorVoltaSeg;
				p.melhorVolta = formatarSegundosParaHMS(melhorVoltaSeg);
			} else {
				p.situacao = cronometrandoAtivo && temLargadaAberta ? 'EM_CURSO' : 'DNF';
				p.tempoTotalSegundos = -1;
				p.tempoTotal = '';
				p.melhorVoltaSegundos = -1;
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
			if (et && et.status === 'OK' && et.duracaoSegundos >= 0) {
				concluintes.push(p);
			}
		}

		concluintes.sort((a, b) => {
			const ta = a.etapas[eIdx].duracaoSegundos;
			const tb = b.etapas[eIdx].duracaoSegundos;
			if (ta !== tb) return ta - tb;
			return a.numeral - b.numeral;
		});

		if (concluintes.length > 0) {
			const melhor = concluintes[0].etapas[eIdx].duracaoSegundos;
			concluintes.forEach((p, rank) => {
				p.etapas[eIdx].posicaoEtapa = rank + 1;
				p.etapas[eIdx].gapEtapa = formatarGapSegundos(
					p.etapas[eIdx].duracaoSegundos - melhor
				);
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

