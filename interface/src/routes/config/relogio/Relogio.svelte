<script lang="ts">
	import Flag from '~icons/tabler/flag';
	import SettingsCard from '$lib/components/SettingsCard.svelte';
	import { page } from '$app/state';
	import { notifications } from '$lib/components/toasts/notifications';
	import { user } from '$lib/stores/user';
	import { on } from 'svelte/events';
	import { onMount } from 'svelte';

	import type { Relogio } from '$lib/types/models';
	import { getRelogio, salvarRelogio } from '$lib/services/services';
	let relogio: Relogio = {
		datetime: ''
	};

	onMount(() => {
		getRelogio().then(data => {
			if (data) {
				relogio = {...data};
				notifications.success('Relogio carregado com sucesso!', 3000);
			}
		}).catch(error => {
			console.error('Erro ao carregar o relogio:', error);
			notifications.error('Erro ao carregar o relogio. Tente novamente.', 3000);
		});
	});

	async function salvar() {
		try {
			const relogioData = await salvarRelogio(relogio);
			if (relogioData) {
				relogio = {...relogioData};
				notifications.success('Relogio salvo com sucesso!', 3000);
			} else {
				notifications.error('Erro ao salvar o relogio. Verifique as permissões.', 3000);
			}
		} catch (error) {
			console.error('Erro ao salvar o relogio:', error);
			notifications.error('Erro ao salvar o relogio. Tente novamente.', 3000);
		}
	}
</script>

<SettingsCard collapsible={false}>
	<div class="space-y-6">
		<h1 class="text-2xl font-bold">Cadastro da Relogio</h1>

		<div class="bg-white p-6 rounded shadow max-w-xl">

			<label for="data" class="block mb-2 font-medium">Data que vai acontecer</label>
			<input
				id="data"
				type="datetime-local"
				bind:value={relogio.datetime}
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
