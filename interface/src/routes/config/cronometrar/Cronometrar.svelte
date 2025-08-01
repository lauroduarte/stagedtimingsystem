<script lang="ts">
	import Flag from '~icons/tabler/flag';
	import SettingsCard from '$lib/components/SettingsCard.svelte';
	import { page } from '$app/state';
	import { notifications } from '$lib/components/toasts/notifications';
	import { user } from '$lib/stores/user';
	import { on } from 'svelte/events';
	import { onMount } from 'svelte';

	import type { Cronometrar } from '$lib/types/models';
	import { getCronometrar, salvarCronometrar } from '$lib/services/services';
	let cronometrar: Cronometrar = $state({
		cronometrar: false,
		tipo: 'especial',
		especial: '',
		posicao: 'inicio'
	});

	onMount(() => {
		getCronometrar().then(data => {
			if (data) {
				cronometrar = {...data};
				notifications.success('Cronometrar carregada com sucesso!', 3000);
			}
		}).catch(error => {
			console.error('Erro ao carregar a cronometrar:', error);
			notifications.error('Erro ao carregar a cronometrar. Tente novamente.', 3000);
		});
	});

	async function salvar() {
		try {
			const cronometrarData = await salvarCronometrar(cronometrar);
			if (cronometrarData) {
				cronometrar = {...cronometrarData};
				cronometrar.cronometrar = !cronometrar.cronometrar; // Toggle cronometrar state
				notifications.success('Cronometrar salva com sucesso!', 3000);
			} else {
				notifications.error('Erro ao salvar a cronometrar. Verifique as permissões.', 3000);
			}
		} catch (error) {
			console.error('Erro ao salvar a cronometrar:', error);
			notifications.error('Erro ao salvar a cronometrar. Tente novamente.', 3000);
		}
	}
</script>

<SettingsCard collapsible={false}>
	<div class="space-y-6">
		<h1 class="text-2xl font-bold">Cadastro da Cronometrar</h1>

		<div class="bg-white p-6 rounded shadow max-w-xl">
			<label for="nome" class="block mb-2 font-medium">Tipo</label>
			<select
				id="tipo"
				bind:value={cronometrar.tipo}
				class="w-full border rounded px-3 py-2 mb-4 focus:outline-none focus:ring-2 focus:ring-blue-500">
				<option value="especial">Especial</option>
				<option value="laps">Laps</option>
			</select>

			{#if cronometrar.tipo === 'especial'}
			<label for="especial" class="block mb-2 font-medium">Especial</label>
			<select 
				id="especial"
				bind:value={cronometrar.especial}
				class="w-full border rounded px-3 py-2 mb-4 focus:outline-none focus:ring-2 focus:ring-blue-500">
				<option value="E1">E1</option>
				<option value="E2">E2</option>
				<option value="E3">E3</option>
				<option value="E4">E4</option>
				<option value="E5">E5</option>
				<option value="E6">E6</option>
				<option value="E7">E7</option>
				<option value="E8">E8</option>
				<option value="E9">E9</option>
				<option value="E10">E10</option>
			</select>
			{/if}
			
			<label for="posicao" class="block mb-2 font-medium">Posição do equipamento</label>
			<select
				id="posicao"
				bind:value={cronometrar.posicao}
				class="w-full border rounded px-3 py-2 mb-4 focus:outline-none focus:ring-2 focus:ring-blue-500">
				<option value="inicio">Início</option>
				<option value="fim">Fim</option>
			</select>

			<button
				on:click={salvar}
				class="bg-blue-600 text-white px-4 py-2 rounded hover:bg-blue-700 transition"
			>
			{#if cronometrar.cronometrar}
				Iniciar
			{:else}
				Parar
			{/if}
			</button>
		</div>
	</div>
</SettingsCard>
