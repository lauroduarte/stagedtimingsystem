export type Relogio = {
	datetime: string;
}

export type Prova = {
	nome: string;
	data: string;
	local: string;
	tipo?: string; // 'especial' | 'laps'
	modoVoltas?: string; // 'unica' | 'duas'
	criterioVoltas?: string; // 'voltas_tempo' | 'melhor_volta'
	totalEstagios?: number;
}

export type WifiStatus = {
	status: number;
	local_ip: string;
	mac_address: string;
	rssi: number;
	ssid: string;
	bssid: string;
	channel: number;
	subnet_mask: string;
	gateway_ip: string;
	dns_ip_1: string;
	dns_ip_2?: string;
};

export type WifiSettings = {
	hostname: string;
	connection_mode: number;
	wifi_networks: KnownNetworkItem[];
};

export type KnownNetworkItem = {
	ssid: string;
	password: string;
	static_ip_config: boolean;
	local_ip?: string;
	subnet_mask?: string;
	gateway_ip?: string;
	dns_ip_1?: string;
	dns_ip_2?: string;
};

export type NetworkItem = {
	rssi: number;
	ssid: string;
	bssid: string;
	channel: number;
	encryption_type: number;
};

export type ApStatus = {
	status: number;
	ip_address: string;
	mac_address: string;
	station_num: number;
};

export type ApSettings = {
	provision_mode: number;
	ssid: string;
	password: string;
	channel: number;
	ssid_hidden: boolean;
	max_clients: number;
	local_ip: string;
	gateway_ip: string;
	subnet_mask: string;
};

export type LightState = {
	led_on: boolean;
};

export type ProvaState = {
	nome: string;
};

export type BrokerSettings = {
	mqtt_path: string;
	name: string;
	unique_id: string;
};

export type NTPStatus = {
	status: number;
	utc_time: string;
	local_time: string;
	server: string;
	uptime: number;
};

export type NTPSettings = {
	enabled: boolean;
	server: string;
	tz_label: string;
	tz_format: string;
};

export type Analytics = {
	max_alloc_heap: number;
	psram_size: number;
	free_psram: number;
	used_psram: number;
	free_heap: number;
	used_heap: number;
	total_heap: number;
	min_free_heap: number;
	core_temp: number;
	fs_total: number;
	fs_used: number;
	uptime: number;
};

export type RSSI = {
	rssi: number;
	ssid: string;
};

export type Battery = {
	soc: number;
	charging: boolean;
};

export type DownloadOTA = {
	status: string;
	progress: number;
	error: string;
};

export type StaticSystemInformation = {
	esp_platform: string;
	firmware_version: string;
	cpu_freq_mhz: number;
	cpu_type: string;
	cpu_rev: number;
	cpu_cores: number;
	sketch_size: number;
	free_sketch_space: number;
	sdk_version: string;
	arduino_version: string;
	flash_chip_size: number;
	flash_chip_speed: number;
	cpu_reset_reason: string;
};

export type SystemInformation = Analytics & StaticSystemInformation;

export type MQTTStatus = {
	enabled: boolean;
	connected: boolean;
	client_id: string;
	last_error: string;
};

export type MQTTSettings = {
	enabled: boolean;
	uri: string;
	username: string;
	password: string;
	client_id: string;
	keep_alive: number;
	clean_session: boolean;
};

export type Atleta = {
	nome: string;
	equipe: string;
	numeral: number;
	categoriaId: number;
};

export type Categoria = {
	id: number;
	categoria: string;
};

export type RFIDCard = {
	uid: string;
	numero: number;
}

export type Cronometrar = {
	cronometrar: boolean;
	tipo: string; // 'especial' | 'laps'
	modoVoltas?: string; // 'unica' | 'duas'
	especial: string;
	posicao: string; // 'inicio' | 'fim'
	intervaloMinVoltaSegundos?: number;
}

export type CronometragemItem = {
	uid: string;
	numeral?: number;
	tipo?: string;
	modoVoltas?: string;
	especial: string;
	volta?: number;
	inicio: string;
	fim: string;
	duracao?: string;
	duracaoSegundos?: number;
	status?: string;
	incompleto?: boolean;
}

export type SyncDiagnostico = {
	totalRegistros: number;
	concluidos: number;
	pendentesChegada: number;
	semLargada: number;
	invalidos: number;
};

export type SyncConflito = {
	uid: string;
	numeral: number;
	especial: string;
	campo: string;
	valorLocal: string;
	valorRemoto: string;
};

export type SyncStatusResponse = {
	status: string;
	versao?: number;
	relogio: string;
	unixtime?: number;
	prova?: string;
	tipoProva?: string;
	modoVoltas?: string;
	criterioVoltas?: string;
	totalEstagios?: number;
	cronometrando?: boolean;
	tipoEstacao?: string;
	especial?: string;
	posicao?: string;
	categorias?: number;
	atletas?: number;
	cartoes?: number;
	registros?: number;
	diagnostico?: SyncDiagnostico;
};

export type SyncConfigPackage = {
	versao?: number;
	relogio?: {
		datetime: string;
		unixtime?: number;
		offsetSeconds?: number;
	};
	prova?: Prova;
	categorias?: Categoria[];
	atletas?: Atleta[];
	rfid?: {
		cards: RFIDCard[];
	};
	cronometrar?: Cronometrar;
	ajustarRelogio?: boolean;
};

export type SyncConfigResponse = {
	status: string;
	provaSincronizada: boolean;
	categoriasSincronizadas: number;
	atletasSincronizados: number;
	cartoesSincronizados: number;
	relogioAjustado: boolean;
	horarioAtual: string;
	mensagem?: string;
};

export type SyncResultadosPackage = {
	versao?: number;
	relogio?: {
		datetime: string;
		unixtime?: number;
	};
	prova?: Prova;
	estacao?: Cronometrar;
	diagnostico?: SyncDiagnostico;
	cronometragem: CronometragemItem[];
};

export type SyncResultadosMergeResponse = {
	status: string;
	recebidos: number;
	inseridos: number;
	conciliados: number;
	duplicadosIgnorados: number;
	conflitosDetectados: number;
	totalConsolidado: number;
	diagnostico: SyncDiagnostico;
	conflitos: SyncConflito[];
	mensagem?: string;
};

export type CriterioVoltas = 'voltas_tempo' | 'melhor_volta';

export type SituacaoPiloto = 'CLASSIFICADO' | 'EM_CURSO' | 'DNF' | 'DNS';

export type StatusResultadoProva = 'oficial' | 'parcial' | 'aguardando';

export type EtapaApurada = {
	codigo: string;
	indice: number;
	inicio: string;
	fim: string;
	duracao: string;
	duracaoSegundos: number;
	status: string; // 'OK' | 'largada' | 'sem_largada' | 'invalido' | 'pendente'
	posicaoEtapa: number;
	gapEtapa: string;
};

export type PilotoApurado = {
	numeral: number;
	nome: string;
	equipe: string;
	categoriaId: number;
	categoriaNome: string;
	uid: string;
	situacao: SituacaoPiloto;
	etapasConcluidas: number;
	etapasEsperadas: number;
	voltasConcluidas: number;
	tempoTotalSegundos: number;
	tempoTotal: string;
	melhorVoltaSegundos: number;
	melhorVolta: string;
	posicaoGeral: number;
	gapGeral: string;
	posicaoCategoria: number;
	gapCategoria: string;
	horarioLargadaInicial: string;
	etapas: EtapaApurada[];
};

export type RelatorioResultadosPublicos = {
	versao?: number;
	resultadoOficial: boolean;
	statusResultado: StatusResultadoProva;
	modoProva: string; // 'especial' | 'laps'
	modoVoltas: string; // 'unica' | 'duas'
	criterioVoltas: CriterioVoltas;
	totalEstagiosConfigurados: number;
	totalEtapasDisputadas: number;
	relogio?: {
		datetime: string;
		unixtime?: number;
	};
	prova?: Prova;
	estacao?: Cronometrar;
	diagnostico: SyncDiagnostico;
	codigosEtapas: string[];
	categorias: Categoria[];
	classificacao: PilotoApurado[];
	cronometragem: CronometragemItem[];
};


