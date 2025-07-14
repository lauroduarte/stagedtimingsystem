import { page } from '$app/state';
import { user } from '$lib/stores/user';
import type { Atleta, Categoria, Prova, Relogio, RFIDCard } from '$lib/types/models';


let $user: {
    username: string;
    admin: boolean;
    bearer_token: string;
}

export function setUser($u: { username: string; admin: boolean; bearer_token: string }) {
	$user = $u;
}

export async function getAtletas(): Promise<Atleta[]> {
	try {
		const response = await fetch('/rest/atleta', {
			method: 'GET',
			headers: {
				Authorization: page.data.features.security ? 'Bearer ' + $user.bearer_token : 'Basic',
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
				Authorization: page.data.features.security ? 'Bearer ' + $user.bearer_token : 'Basic',
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

export async function getProva(): Promise<Prova | undefined> {
	try {
		const response = await fetch('/rest/prova', {
			method: 'GET',
			headers: {
				Authorization: page.data.features.security ? 'Bearer ' + $user.bearer_token : 'Basic',
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
				Authorization: page.data.features.security ? 'Bearer ' + $user.bearer_token : 'Basic',
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
				Authorization: page.data.features.security ? 'Bearer ' + $user.bearer_token : 'Basic',
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
				Authorization: page.data.features.security ? 'Bearer ' + $user.bearer_token : 'Basic',
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

export async function salvarProva(prova: Prova): Promise<Prova> {
	try {
		const response = await fetch('/rest/prova', {
			method: 'POST',
			headers: {
				Authorization: page.data.features.security ? 'Bearer ' + $user.bearer_token : 'Basic',
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
				Authorization: page.data.features.security ? 'Bearer ' + $user.bearer_token : 'Basic',
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

export async function startRFIDReset(): Promise<void> {
	try {
		const response = await fetch('/rest/rfid/start_reset', {
			method: 'GET',
			headers: {
				Authorization: page.data.features.security ? 'Bearer ' + $user.bearer_token : 'Basic',
				'Content-Type': 'application/json'
			}
		});
		if (response.status !== 200) {
			throw new Error(`Erro ao iniciar o reset do RFID: ${response.status} ${response.statusText}`);
		}
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
				Authorization: page.data.features.security ? 'Bearer ' + $user.bearer_token : 'Basic',
				'Content-Type': 'application/json'
			}
		});
		if (response.status === 200) {
			const data = await response.json();
			const cards: RFIDCard[] = data || [];
			return cards;
		} else {
			throw new Error(`Erro ao parar o reset do RFID: ${response.status} ${response.statusText}`);
		}
	} catch (error) {
		console.error('Erro ao parar o reset do RFID:', error);
		throw new Error('Erro ao parar o reset do RFID');
	}
}
