<script lang="ts">
	import Flag from '~icons/tabler/flag';
	import SettingsCard from '$lib/components/SettingsCard.svelte';
	import { page } from '$app/state';
	import { notifications } from '$lib/components/toasts/notifications';
	import { user } from '$lib/stores/user';
	import { on } from 'svelte/events';
	import { onMount } from 'svelte';

	import type { Prova } from '$lib/types/models';
	import { getProva, salvarProva } from '$lib/services/services';
	let prova: Prova = {
		nome: '',
		data: '',
		local: ''
	};

	onMount(() => {
		getProva().then(data => {
			if (data) {
				prova = {...data};
				notifications.success('Prova carregada com sucesso!', 3000);
			}
		}).catch(error => {
			console.error('Erro ao carregar a prova:', error);
			notifications.error('Erro ao carregar a prova. Tente novamente.', 3000);
		});
	});

	async function salvar() {
		try {
			const provaData = await salvarProva(prova);
			if (provaData) {
				prova = {...provaData};
				notifications.success('Prova salva com sucesso!', 3000);
			} else {
				notifications.error('Erro ao salvar a prova. Verifique as permissões.', 3000);
			}
		} catch (error) {
			console.error('Erro ao salvar a prova:', error);
			notifications.error('Erro ao salvar a prova. Tente novamente.', 3000);
		}
	}
</script>

<SettingsCard collapsible={false}>
	<div class="space-y-6">
		<h1 class="text-2xl font-bold">Cadastro da Prova</h1>

		<div class="bg-white p-6 rounded shadow max-w-xl">
			<label for="nome" class="block mb-2 font-medium">Nome da Prova</label>
			<input
				id="nome"
				type="text"
				bind:value={prova.nome}
				class="w-full border rounded px-3 py-2 mb-4 focus:outline-none focus:ring-2 focus:ring-blue-500"
			/>

			<label for="data" class="block mb-2 font-medium">Data que vai acontecer</label>
			<input
				id="data"
				type="date"
				bind:value={prova.data}
				class="w-full border rounded px-3 py-2 mb-4 focus:outline-none focus:ring-2 focus:ring-blue-500"
			/>
			
			<label for="local" class="block mb-2 font-medium">Local de realização</label>
			<input
				id="local"
				type="text"
				bind:value={prova.local}
				class="w-full border rounded px-3 py-2 mb-4 focus:outline-none focus:ring-2 focus:ring-blue-500"
			/>			

			<button
				on:click={salvar}
				class="bg-blue-600 text-white px-4 py-2 rounded hover:bg-blue-700 transition"
			>
				Salvar
			</button>
		</div>
	</div>
</SettingsCard>
